#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9034
#define BUFFER_SIZE 1024

std::vector<SOCKET> clients;
std::mutex clients_mutex;
std::atomic<bool> running(true);

void broadcast(const std::string& message, SOCKET sender_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (SOCKET client : clients) {
        if (client != sender_socket) {
            send(client, message.c_str(), message.size(), 0);
        }
    }
}

void close_all_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (SOCKET client : clients) {
        closesocket(client);
    }
    clients.clear();
}

BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        std::cout << "\nGracefully shutting down server..." << std::endl;
        running = false;
        return TRUE;
    }
    return FALSE;
}

void handle_client(SOCKET client_socket) {
    char buffer[BUFFER_SIZE];
    int bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        closesocket(client_socket);
        return;
    }
    buffer[bytes] = '\0';
    std::string username(buffer);
    std::string welcome = username + " joined the chat\n";
    std::cout << welcome;
    broadcast(welcome, client_socket);

    while (running) {
        bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        std::string msg = username + ": " + buffer;
        std::cout << msg;
        broadcast(msg, client_socket);
    }

    closesocket(client_socket);
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = std::find(clients.begin(), clients.end(), client_socket);
        if (it != clients.end()) clients.erase(it);
    }
    std::string leave = username + " left the chat\n";
    std::cout << leave;
    broadcast(leave, INVALID_SOCKET);
}

int main() {
    SetConsoleCtrlHandler(console_handler, TRUE);

    WSADATA wsaData;
    SOCKET server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        WSACleanup();
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed" << std::endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, 3) == SOCKET_ERROR) {
        std::cerr << "Listen failed" << std::endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    std::cout << "Server listening on port " << PORT << std::endl;
    std::cout << "Press Ctrl+C to gracefully shut down." << std::endl;

    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        struct timeval tv = {1, 0};
        if (select(0, &readfds, nullptr, nullptr, &tv) == SOCKET_ERROR) {
            if (!running) break;
            continue;
        }
        if (FD_ISSET(server_fd, &readfds)) {
            if ((client_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen)) == INVALID_SOCKET) {
                if (!running) break;
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

    std::cout << "Shutting down. Notifying clients..." << std::endl;
    broadcast("SERVER_SHUTDOWN\n", INVALID_SOCKET);
    Sleep(1000);
    close_all_clients();
    closesocket(server_fd);
    WSACleanup();
    std::cout << "Server stopped." << std::endl;
    return 0;
}