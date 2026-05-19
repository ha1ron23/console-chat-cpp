#include <iostream>
#include <string>
#include <cstring>
#include <atomic>
#include <thread>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <termios.h>
#include <fcntl.h>

#define PORT 9034
#define BUFFER_SIZE 1024

std::atomic<bool> running(true);
int sock = -1;
std::string input_buffer;

void set_nonblocking_input(bool enable) {
    static struct termios oldt, newt;
    if (enable) {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        fcntl(STDIN_FILENO, F_SETFL, 0);
    }
}

void redraw_prompt() {
    std::cout << "\r\033[K> " << input_buffer << std::flush;
}

void add_message(const std::string& msg) {
    std::cout << "\r\033[K" << msg << std::flush;
    redraw_prompt();
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
        add_message(msg);
    }
    close(sock);
    exit(0);
}

int main() {
    std::string username;
    std::cout << "Enter your username: ";
    std::getline(std::cin, username);

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return 1;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        return 1;
    }
    send(sock, username.c_str(), username.size(), 0);

    set_nonblocking_input(true);
    std::cout << "Connected to chat. Type /quit to exit.\n" << std::flush;
    redraw_prompt();

    std::thread receiver(receive_messages);
    receiver.detach();

    char ch;
    while (running) {
        int n = read(STDIN_FILENO, &ch, 1);
        if (n == 1) {
            if (ch == '\n') {
                if (!input_buffer.empty()) {
                    std::string out = input_buffer + "\n";
                    send(sock, out.c_str(), out.size(), 0);
                    if (input_buffer == "/quit") break;
                    input_buffer.clear();
                    redraw_prompt();
                } else {
                    redraw_prompt();
                }
            } else if (ch == 127 || ch == 8) { // Backspace
                if (!input_buffer.empty()) {
                    input_buffer.pop_back();
                    redraw_prompt();
                }
            } else if (ch >= 32 && ch <= 126) {
                input_buffer.push_back(ch);
                redraw_prompt();
            }
        }
        usleep(10000);
    }

    running = false;
    set_nonblocking_input(false);
    close(sock);
    std::cout << "\nGoodbye.\n";
    return 0;
}
