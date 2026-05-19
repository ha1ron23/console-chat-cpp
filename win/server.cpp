#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <ctime>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9034
#define BUFFER_SIZE 1024

std::unordered_map<std::string, SOCKET> clients;
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

void send_to_client(SOCKET s, const std::string& msg) {
    send(s, msg.c_str(), msg.size(), 0);
}

void broadcast(const std::string& message, SOCKET sender) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto& p : clients) {
        if (p.second != sender) {
            send_to_client(p.second, message);
        }
    }
}

bool send_private(const std::string& target, const std::string& msg, SOCKET sender_sock, const std::string& sender_name) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    auto it = clients.find(target);
    if (it == clients.end()) return false;
    std::string formatted = "[PM from " + sender_name + "]: " + msg + "\n";
    send_to_client(it->second, formatted);
    send_to_client(sender_sock, "[PM to " + target + "]: " + msg + "\n");
    return true;
}

void remove_client(const std::string& username, SOCKET s) {
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(username);
    }
    std::string leave = username + " left the chat\n";
    std::cout << leave;
    broadcast(leave, s);
    log_message(leave);
    closesocket(s);
}

void handle_client(SOCKET client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        closesocket(client_sock);
        return;
    }
    buffer[bytes] = '\0';
    std::string username(buffer);
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        if (clients.find(username) != clients.end()) {
            send_to_client(client_sock, "USERNAME_TAKEN\n");
            closesocket(client_sock);
            return;
        }
        clients[username] = client_sock;
    }
    std::string welcome = username + " joined the chat\n";
    std::cout << welcome;
    broadcast(welcome, client_sock);
    log_message(welcome);

    while (running) {
        bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        std::string message(buffer);
        if (!message.empty() && message.back() == '\n') message.pop_back();

        if (message.substr(0, 5) == "/msg ") {
            size_t space = message.find(' ', 5);
            if (space != std::string::npos) {
                std::string target = message.substr(5, space - 5);
                std::string pm_msg = message.substr(space + 1);
                if (target == username) {
                    send_to_client(client_sock, "You cannot message yourself.\n");
                } else if (!send_private(target, pm_msg, client_sock, username)) {
                    send_to_client(client_sock, "User " + target + " not online.\n");
                } else {
                    log_message("[PM] " + username + " -> " + target + ": " + pm_msg);
                }
            } else {
                send_to_client(client_sock, "Usage: /msg <username> <message>\n");
            }
        }
        else if (message == "/users") {
            std::string list = "Online users: ";
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto& p : clients) list += p.first + " ";
            list += "\n";
            send_to_client(client_sock, list);
        }
        else if (message == "/quit") {
            break;
        }
        else {
            std::string formatted = username + ": " + message + "\n";
            std::cout << formatted;
            broadcast(formatted, client_sock);
            log_message("[PUB] " + username + ": " + message);
        }
    }
    remove_client(username, client_sock);
}

void close_all_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto& p : clients) closesocket(p.second);
    clients.clear();
}

BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        std::cout << "\nShutting down server...\n";
        running = false;
        return TRUE;
    }
    return FALSE;
}

int main() {
    SetConsoleCtrlHandler(console_handler, TRUE);
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET server_fd, client_sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed\n";
        return 1;
    }
    if (listen(server_fd, 3) == SOCKET_ERROR) {
        std::cerr << "Listen failed\n";
        return 1;
    }
    std::cout << "Server listening on port " << PORT << std::endl;

    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        struct timeval tv = {1, 0};
        if (select(0, &readfds, nullptr, nullptr, &tv) == SOCKET_ERROR) continue;
        if (FD_ISSET(server_fd, &readfds)) {
            client_sock = accept(server_fd, (struct sockaddr*)&address, &addrlen);
            if (client_sock == INVALID_SOCKET) continue;
            std::thread(handle_client, client_sock).detach();
            std::cout << "New client connected\n";
        }
    }

    broadcast("SERVER_SHUTDOWN\n", INVALID_SOCKET);
    Sleep(1000);
    close_all_clients();
    closesocket(server_fd);
    WSACleanup();
    if (logfile.is_open()) logfile.close();
    std::cout << "Server stopped.\n";
    return 0;
}
