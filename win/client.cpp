#include <iostream>
#include <string>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
#include <conio.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9034
#define BUFFER_SIZE 1024

std::atomic<bool> running(true);
SOCKET sock = INVALID_SOCKET;
std::string input_buffer;

void add_message(const std::string& msg) {
    std::cout << "\r\033[K" << msg << std::flush;
    std::cout << "\r> " << input_buffer << std::flush;
}

void receive_messages() {
    char buffer[BUFFER_SIZE];
    while (running) {
        int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            add_message("\n[System] Server disconnected. Exiting...\n");
            running = false;
            break;
        }
        buffer[bytes] = '\0';
        std::string msg(buffer);
        if (msg == "SERVER_SHUTDOWN\n") {
            add_message("\n[System] Server shutting down. Exiting...\n");
            running = false;
            break;
        }
        add_message(msg);
    }
    closesocket(sock);
    WSACleanup();
    exit(0);
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::string username;
    std::cout << "Enter your username: ";
    std::getline(std::cin, username);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Connect failed. Is server running?\n";
        return 1;
    }
    send(sock, username.c_str(), username.size(), 0);

    std::cout << "Connected. Type /quit to exit.\n";
    std::cout << "> " << std::flush;

    std::thread receiver(receive_messages);
    receiver.detach();

    while (running) {
        if (_kbhit()) {
            char ch = _getch();
            if (ch == '\r') {  // Enter
                if (!input_buffer.empty()) {
                    std::string out = input_buffer + "\n";
                    send(sock, out.c_str(), out.size(), 0);
                    if (input_buffer == "/quit") break;
                    input_buffer.clear();
                    std::cout << "\r> " << std::flush;
                }
            } else if (ch == '\b') {  // Backspace
                if (!input_buffer.empty()) {
                    input_buffer.pop_back();
                    std::cout << "\b \b" << std::flush;
                }
            } else if (ch >= 32 && ch <= 126) {
                input_buffer.push_back(ch);
                std::cout << ch << std::flush;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    running = false;
    closesocket(sock);
    WSACleanup();
    std::cout << "\nGoodbye.\n";
    return 0;
}
