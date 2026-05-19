#include <iostream>
#include <string>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9034
#define BUFFER_SIZE 4096

std::atomic<bool> running(true);
SOCKET sock = INVALID_SOCKET;
std::string my_username;
std::string my_role = "Member";

void set_utf8() {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
}

void add_message(const std::string& msg) {
    std::cout << msg << std::flush;
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
            add_message("\n[System] Server is shutting down. Exiting...\n");
            running = false;
            break;
        }
        if (msg.substr(0, 10) == "YOUR_ROLE ") {
            std::string rest = msg.substr(10);
            size_t sp = rest.find(' ');
            if (sp != std::string::npos) {
                my_role = rest.substr(0, sp);
                std::string cmd_list = rest.substr(sp+1);
                std::cout << "Your role: " << my_role << "\n";
                if (!cmd_list.empty())
                    std::cout << "Commands: " << cmd_list << "\n";
            }
            continue;
        }
        if (msg.substr(0, 13) == "ROLE_CHANGED ") {
            std::string rest = msg.substr(13);
            size_t sp = rest.find(' ');
            if (sp != std::string::npos) {
                my_role = rest.substr(0, sp);
                add_message("Your role changed to " + my_role + "\n");
            }
            continue;
        }
        if (msg.find("Commands available:") != std::string::npos) continue;
        size_t colon = msg.find(':');
        if (colon != std::string::npos) {
            std::string before = msg.substr(0, colon);
            std::string after = msg.substr(colon + 1);
            size_t bracket_open = before.find('[');
            size_t bracket_close = before.find(']');
            if (bracket_open != std::string::npos && bracket_close != std::string::npos) {
                std::string role_part = before.substr(bracket_open, bracket_close - bracket_open + 1);
                std::string name_part = before.substr(bracket_close + 1);
                name_part.erase(0, name_part.find_first_not_of(' '));
                name_part.erase(name_part.find_last_not_of(' ') + 1);
                if (name_part == my_username) {
                    msg = role_part + " " + name_part + " (you):" + after;
                } else {
                    msg = before + ":" + after;
                }
            }
        }
        add_message(msg);
    }
    closesocket(sock);
    WSACleanup();
    exit(0);
}

int main() {
    set_utf8();
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::cout << "Enter your username: ";
    std::getline(std::cin, my_username);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Connect failed. Is server running?\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    send(sock, my_username.c_str(), my_username.size(), 0);

    std::cout << "Connected. Type /quit to exit.\n";

    std::thread receiver(receive_messages);
    receiver.detach();

    std::string input;
    while (running) {
        if (!std::getline(std::cin, input)) break;
        if (input == "/quit") break;
        std::string out = input + "\n";
        send(sock, out.c_str(), out.size(), 0);
    }

    running = false;
    closesocket(sock);
    WSACleanup();
    std::cout << "\nGoodbye.\n";
    return 0;
}
