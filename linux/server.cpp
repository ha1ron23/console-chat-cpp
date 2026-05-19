#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include <csignal>
#include <atomic>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9034
#define BUFFER_SIZE 1024

std::vector<int> clients;
std::mutex clients_mutex;
std::atomic<bool> running(true);

void broadcast(const std::string& message, int sender_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (int client : clients) {
        if (client != sender_socket) {
            send(client, message.c_str(), message.size(), 0);
        }
    }
}

void close_all_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (int client : clients) {
        close(client);
    }
    clients.clear();
}

void signal_handler(int signum) {
    (void)signum;
    std::cout << "\nGracefully shutting down server..." << std::endl;
    running = false;
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
    std::string welcome = username + " joined the chat\n";
    std::cout << welcome;
    broadcast(welcome, client_socket);

    // Message loop
    while (running) {
        bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;  // client disconnected or error
        buffer[bytes] = '\0';
        std::string msg = username + ": " + buffer;
        std::cout << msg;
        broadcast(msg, client_socket);
    }

    // Client disconnected
    close(client_socket);
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = std::find(clients.begin(), clients.end(), client_socket);
        if (it != clients.end()) clients.erase(it);
    }
    std::string leave = username + " left the chat\n";
    std::cout << leave;
    broadcast(leave, -1);
}

int main() {
    signal(SIGINT, signal_handler);

    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create socket
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

    // Bind
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    // Listen
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on port " << PORT << std::endl;
    std::cout << "Press Ctrl+C to gracefully shut down." << std::endl;

    // Main accept loop
    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        // Wait with timeout to allow checking running flag
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
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients.push_back(client_socket);
            }
            std::thread(handle_client, client_socket).detach();
            std::cout << "New client connected. Total: " << clients.size() << std::endl;
        }
    }

    // Graceful shutdown: notify clients
    std::cout << "Shutting down. Notifying clients..." << std::endl;
    broadcast("SERVER_SHUTDOWN\n", -1);
    sleep(1); // Give clients time to receive message
    close_all_clients();
    close(server_fd);
    std::cout << "Server stopped." << std::endl;
    return 0;
}