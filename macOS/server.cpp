#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <csignal>
#include <atomic>
#include <fstream>
#include <ctime>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9034
#define BUFFER_SIZE 1024

std::unordered_map<std::string, int> clients;   // username -> socket
std::mutex clients_mutex;
std::atomic<bool> running(true);
std::ofstream logfile("chat.log", std::ios::app);

std::string current_time() {
    time_t now = time(nullptr);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}

void log_message(const std::string& msg) {
    if (logfile.is_open()) {
        logfile << "[" << current_time() << "] " << msg << std::endl;
        logfile.flush();
    }
}

void send_to_client(int socket, const std::string& msg) {
    send(socket, msg.c_str(), msg.size(), 0);
}

void broadcast(const std::string& message, int sender_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto& pair : clients) {
        if (pair.second != sender_socket) {
            send_to_client(pair.second, message);
        }
    }
}

bool send_private(const std::string& target_username, const std::string& msg, int sender_socket, const std::string& sender_username) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    auto it = clients.find(target_username);
    if (it == clients.end()) return false;
    std::string formatted = "[PM from " + sender_username + "]: " + msg + "\n";
    send_to_client(it->second, formatted);
    // Also send confirmation to sender
    std::string confirm = "[PM to " + target_username + "]: " + msg + "\n";
    send_to_client(sender_socket, confirm);
    return true;
}

void remove_client(const std::string& username, int socket) {
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(username);
    }
    std::string leave = username + " left the chat\n";
    std::cout << leave;
    broadcast(leave, socket);
    log_message(leave);
    close(socket);
}

void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    // Receive username
    int bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        close(client_socket);
        return;
    }
    buffer[bytes] = '\0';
    std::string username(buffer);

    // Check if username already taken
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        if (clients.find(username) != clients.end()) {
            send_to_client(client_socket, "USERNAME_TAKEN\n");
            close(client_socket);
            return;
        }
        clients[username] = client_socket;
    }

    std::string welcome = username + " joined the chat\n";
    std::cout << welcome;
    broadcast(welcome, client_socket);
    log_message(welcome);

    // Message loop
    while (running) {
        bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        std::string message(buffer);

        // Remove trailing newline if any
        if (!message.empty() && message.back() == '\n')
            message.pop_back();

        // Handle commands
        if (message.substr(0, 5) == "/msg ") {
            size_t first_space = message.find(' ', 5);
            if (first_space != std::string::npos) {
                std::string target = message.substr(5, first_space - 5);
                std::string private_msg = message.substr(first_space + 1);
                if (target == username) {
                    send_to_client(client_socket, "You cannot send a private message to yourself.\n");
                } else if (!send_private(target, private_msg, client_socket, username)) {
                    send_to_client(client_socket, "User " + target + " not online.\n");
                } else {
                    log_message("[PM] " + username + " -> " + target + ": " + private_msg);
                }
            } else {
                send_to_client(client_socket, "Usage: /msg <username> <message>\n");
            }
        }
        else if (message == "/users") {
            std::string list = "Online users: ";
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto& pair : clients) {
                list += pair.first + " ";
            }
            list += "\n";
            send_to_client(client_socket, list);
        }
        else if (message == "/quit") {
            break;
        }
        else {
            // Public message
            std::string formatted = username + ": " + message + "\n";
            std::cout << formatted;
            broadcast(formatted, client_socket);
            log_message("[PUB] " + username + ": " + message);
        }
    }

    // Client disconnected
    remove_client(username, client_socket);
}

void close_all_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto& pair : clients) {
        close(pair.second);
    }
    clients.clear();
}

void signal_handler(int signum) {
    (void)signum;
    std::cout << "\nGracefully shutting down server..." << std::endl;
    running = false;
}

int main() {
    signal(SIGINT, signal_handler);

    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on port " << PORT << std::endl;
    std::cout << "Press Ctrl+C to gracefully shut down." << std::endl;

    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        struct timeval tv = {1, 0};
        if (select(server_fd + 1, &readfds, nullptr, nullptr, &tv) < 0) {
            if (!running) break;
            perror("select");
            continue;
        }
        if (FD_ISSET(server_fd, &readfds)) {
            if ((client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
                if (!running) break;
                perror("accept");
                continue;
            }
            std::thread(handle_client, client_socket).detach();
            std::cout << "New client connected." << std::endl;
        }
    }

    // Graceful shutdown
    std::cout << "Shutting down. Notifying clients..." << std::endl;
    broadcast("SERVER_SHUTDOWN\n", -1);
    sleep(1);
    close_all_clients();
    close(server_fd);
    if (logfile.is_open()) logfile.close();
    std::cout << "Server stopped." << std::endl;
    return 0;
}
