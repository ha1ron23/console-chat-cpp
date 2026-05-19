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
#include <locale.h>

#define PORT 9034
#define BUFFER_SIZE 4096

std::atomic<bool> running(true);
int sock = -1;
std::string input_buffer;
std::string my_username;
std::string my_role = "Member";

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
    std::cout << "\r\033[K[" << my_role << "] " << my_username << "> " << input_buffer << std::flush;
}

void add_message(const std::string& msg) {
    std::cout << "\r\033[K" << msg << std::flush;
    redraw_prompt();
}

std::string replace_you(const std::string& msg, const std::string& username) {
    size_t colon = msg.find(':');
    if (colon == std::string::npos) return msg;
    std::string before = msg.substr(0, colon);
    std::string after = msg.substr(colon + 1);
    size_t bracket_open = before.find('[');
    size_t bracket_close = before.find(']');
    if (bracket_open == std::string::npos || bracket_close == std::string::npos) return msg;
    std::string role_part = before.substr(bracket_open, bracket_close - bracket_open + 1);
    std::string name_part = before.substr(bracket_close + 1);
    name_part.erase(0, name_part.find_first_not_of(' '));
    name_part.erase(name_part.find_last_not_of(' ') + 1);
    if (name_part == username) {
        return role_part + " " + name_part + " (you):" + after;
    }
    return msg;
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
                std::cout << "\r\033[KYour role: " << my_role << "\n";
                if (!cmd_list.empty())
                    std::cout << "Commands: " << cmd_list << "\n";
                redraw_prompt();
            }
            continue;
        }
        if (msg.substr(0, 13) == "ROLE_CHANGED ") {
            std::string rest = msg.substr(13);
            size_t sp = rest.find(' ');
            if (sp != std::string::npos) {
                my_role = rest.substr(0, sp);
                std::string cmd_list = rest.substr(sp+1);
                add_message("Your role changed to " + my_role + "\n");
                if (!cmd_list.empty())
                    add_message("Commands: " + cmd_list + "\n");
                redraw_prompt();
            }
            continue;
        }
        if (msg.find("Commands available:") != std::string::npos) continue;
        msg = replace_you(msg, my_username);
        add_message(msg);
    }
    close(sock);
    exit(0);
}

int main() {
    setlocale(LC_ALL, "");
    std::cout << "Enter your username: ";
    std::getline(std::cin, my_username);

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
    send(sock, my_username.c_str(), my_username.size(), 0);

    set_nonblocking_input(true);
    std::cout << "Connected. Type /quit to exit.\n";
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
            } else if (ch == 127 || ch == 8) {
                if (!input_buffer.empty()) {
                    input_buffer.pop_back();
                    redraw_prompt();
                }
            } else {
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
