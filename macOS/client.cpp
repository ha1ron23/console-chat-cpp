#include <iostream>
#include <string>
#include <cstring>
#include <atomic>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define PORT 9034
#define BUFFER_SIZE 1024

std::atomic<bool> running(true);
int sock = -1;

bool receive_once() {
    char buffer[BUFFER_SIZE];
    int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) return false;
    buffer[bytes] = '\0';
    std::string msg(buffer);
    if (msg == "SERVER_SHUTDOWN\n") {
        std::cout << "Server is shutting down. Exiting..." << std::endl;
        return false;
    }
    std::cout << msg << std::flush;
    return true;
}

void send_message(const std::string& msg) {
    std::string out = msg + "\n";
    send(sock, out.c_str(), out.size(), 0);
}

int main() {
    struct sockaddr_in server_addr;
    std::string username;

    std::cout << "Enter your username: ";
    std::getline(std::cin, username);

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return 1;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        return 1;
    }

    send(sock, username.c_str(), username.size(), 0);

    fd_set readfds;
    while (running) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);
        struct timeval tv = {1, 0};

        int activity = select(std::max(STDIN_FILENO, sock) + 1, &readfds, nullptr, nullptr, &tv);
        if (activity < 0) {
            perror("select");
            break;
        }
        if (activity == 0) continue;

        if (FD_ISSET(sock, &readfds)) {
            if (!receive_once()) {
                running = false;
                break;
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string input;
            if (!std::getline(std::cin, input)) {
                break;
            }
            if (input == "/quit") break;
            send_message(input);
        }
    }

    close(sock);
    std::cout << "Chat ended: server exit." << std::endl;
    return 0;
}