#include <iostream>
#include <string>
#include <cstring>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <conio.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9034
#define BUFFER_SIZE 1024

std::atomic<bool> running(true);
SOCKET sock = INVALID_SOCKET;

bool receive_once() {
    char buffer[BUFFER_SIZE];
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
    int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        std::string msg(buffer);
        if (msg == "SERVER_SHUTDOWN\n") {
            std::cout << "Server is shutting down. Exiting..." << std::endl;
            return false;
        }
        std::cout << msg << std::flush;
        return true;
    } else if (bytes == 0) {
        std::cout << "Server disconnected. Exiting..." << std::endl;
        return false;
    }
    return true;
}

void send_message(const std::string& msg) {
    std::string out = msg + "\n";
    send(sock, out.c_str(), out.size(), 0);
}

int main() {
    WSADATA wsaData;
    struct sockaddr_in server_addr;
    std::string username;

    std::cout << "Enter your username: ";
    std::getline(std::cin, username);

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Connect failed. Is the server running?" << std::endl;
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    send(sock, username.c_str(), username.size(), 0);

    std::cout << "Connected to chat. Type /quit to exit." << std::endl;

    std::string input;
    bool input_mode = false;

    // Main loop
    while (running) {
        if (!receive_once()) {
            running = false;
            break;
        }

        if (_kbhit()) {
            char ch = _getch();
            if (ch == '\r') {
                if (!input.empty()) {
                    if (input == "/quit") break;
                    send_message(input);
                    input.clear();
                }
                std::cout << std::endl;
                input_mode = false;
            } else if (ch == '\b') {
                if (!input.empty()) {
                    input.pop_back();
                    std::cout << "\b \b";
                }
            } else {
                input.push_back(ch);
                std::cout << ch;
                input_mode = true;
            }
        } else if (input_mode) {
        }

        Sleep(50);
    }

    closesocket(sock);
    WSACleanup();
    std::cout << "Chat ended: server exit." << std::endl;
    return 0;
}