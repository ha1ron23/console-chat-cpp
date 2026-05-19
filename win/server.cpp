#define _WIN32_WINNT 0x0601
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <atomic>
#include <ctime>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9034
#define BUFFER_SIZE 4096
#define USERS_FILE "users.txt"
#define ROLES_FILE "roles.txt"
#define PERMANENT (uint64_t)-1

struct User {
    std::string password;
    std::string role;
    uint64_t ban_until;
    uint64_t mute_until;
    std::string ban_reason;
    std::string mute_reason;
};

struct Client {
    SOCKET socket;
    std::string username;
    std::string role;
    bool muted;
};

std::unordered_map<std::string, User> user_db;
std::unordered_map<std::string, Client> clients;
std::unordered_map<std::string, std::vector<std::string>> role_commands;
std::mutex db_mutex;
std::mutex clients_mutex;
std::atomic<bool> running(true);
std::ofstream logfile("chat.log", std::ios::app);
SOCKET server_fd = INVALID_SOCKET;

uint64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string time_left(uint64_t until) {
    if (until == PERMANENT) return "permanent";
    if (until == 0) return "expired";
    uint64_t left = (until > now_seconds()) ? (until - now_seconds()) : 0;
    if (left == 0) return "expired";
    int days = left / 86400;
    int hours = (left % 86400) / 3600;
    int minutes = (left % 3600) / 60;
    int seconds = left % 60;
    char buf[64];
    sprintf(buf, "%dd %02dh %02dm %02ds", days, hours, minutes, seconds);
    return std::string(buf);
}

void log_message(const std::string& msg) {
    if (logfile.is_open()) {
        char tbuf[32];
        time_t now = time(nullptr);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        logfile << "[" << tbuf << "] " << msg << std::endl;
        logfile.flush();
    }
}

void send_to_client(SOCKET s, const std::string& msg) {
    if (s != INVALID_SOCKET) send(s, msg.c_str(), msg.size(), 0);
}

void broadcast_all(const std::string& message) {
    std::vector<SOCKET> socks;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& p : clients) socks.push_back(p.second.socket);
    }
    for (SOCKET s : socks) send_to_client(s, message);
}

void broadcast(const std::string& message, SOCKET sender_socket) {
    std::vector<SOCKET> socks;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& p : clients) {
            if (p.second.socket != sender_socket)
                socks.push_back(p.second.socket);
        }
    }
    for (SOCKET s : socks) send_to_client(s, message);
}

bool send_private(const std::string& target, const std::string& msg, SOCKET sender_socket, const std::string& sender_name) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    auto it = clients.find(target);
    if (it == clients.end()) return false;
    send_to_client(it->second.socket, "[PM from " + sender_name + "]: " + msg + "\n");
    send_to_client(sender_socket, "[PM to " + target + "]: " + msg + "\n");
    return true;
}

void load_roles() {
    std::ifstream file(ROLES_FILE);
    if (!file.is_open()) {
        std::ofstream out(ROLES_FILE);
        out << "Admin /kick /ban /mute /unban /unmute /users /broadcast /cr\n";
        out << "Member /users\n";
        out << "Moderator /kick /mute /users\n";
        out.close();
        file.open(ROLES_FILE);
        if (!file.is_open()) return;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string role;
        iss >> role;
        std::string cmd;
        std::vector<std::string> cmds;
        while (iss >> cmd) cmds.push_back(cmd);
        role_commands[role] = cmds;
    }
}

std::string get_commands_for_role(const std::string& role) {
    auto it = role_commands.find(role);
    if (it == role_commands.end()) return "";
    std::string result;
    for (const auto& cmd : it->second) result += cmd + " ";
    return result;
}

void load_users() {
    std::ifstream file(USERS_FILE);
    if (!file.is_open()) {
        user_db["admin"] = {"", "Admin", 0, 0, "", ""};
        std::ofstream out(USERS_FILE);
        out << "admin  Admin 0 0 \"\" \"\"\n";
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string user, pass, role, ban_reason, mute_reason;
        uint64_t ban, mute;
        iss >> user >> pass >> role >> ban >> mute;
        std::string rest;
        std::getline(iss, rest);
        if (!rest.empty()) {
            size_t pos = rest.find_first_not_of(' ');
            if (pos != std::string::npos) rest = rest.substr(pos);
            if (rest.front() == '"') {
                size_t end = rest.find('"', 1);
                if (end != std::string::npos) ban_reason = rest.substr(1, end-1);
                rest = rest.substr(end+1);
                rest.erase(0, rest.find_first_not_of(' '));
                if (rest.front() == '"') {
                    end = rest.find('"', 1);
                    if (end != std::string::npos) mute_reason = rest.substr(1, end-1);
                }
            }
        }
        user_db[user] = {pass, role, ban, mute, ban_reason, mute_reason};
    }
}

void save_users() {
    std::ofstream file(USERS_FILE);
    for (auto& p : user_db) {
        file << p.first << " " << p.second.password << " " << p.second.role
             << " " << p.second.ban_until << " " << p.second.mute_until
             << " \"" << p.second.ban_reason << "\" \"" << p.second.mute_reason << "\"\n";
    }
}

uint64_t parse_time(const std::string& str) {
    if (str == "permanent") return PERMANENT;
    int num = 0;
    char unit[10];
    if (sscanf(str.c_str(), "%d%s", &num, unit) != 2) return 0;
    if (num <= 0) return 0;
    if (strcmp(unit, "sec") == 0) return now_seconds() + num;
    if (strcmp(unit, "min") == 0) return now_seconds() + num * 60;
    if (strcmp(unit, "h") == 0) return now_seconds() + num * 3600;
    if (strcmp(unit, "d") == 0) return now_seconds() + num * 86400;
    if (strcmp(unit, "w") == 0) return now_seconds() + num * 604800;
    if (strcmp(unit, "m") == 0) return now_seconds() + num * 2592000;
    if (strcmp(unit, "y") == 0) return now_seconds() + num * 31536000;
    return 0;
}

void kick_user(const std::string& username, const std::string& reason = "") {
    SOCKET sock = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = clients.find(username);
        if (it != clients.end()) {
            sock = it->second.socket;
            if (!reason.empty())
                send_to_client(sock, "You were kicked: " + reason + "\n");
            else
                send_to_client(sock, "You were kicked by admin.\n");
            clients.erase(it);
        }
    }
    if (sock != INVALID_SOCKET) {
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        std::string msg = username + " was kicked";
        if (!reason.empty()) msg += " (reason: " + reason + ")";
        std::cout << msg << std::endl;
        broadcast(msg + "\n", INVALID_SOCKET);
        log_message(msg);
    }
}

void ban_user(const std::string& username, uint64_t until, const std::string& reason) {
    {
        std::lock_guard<std::mutex> lock(db_mutex);
        auto it = user_db.find(username);
        if (it == user_db.end()) return;
        it->second.ban_until = until;
        it->second.ban_reason = reason;
        save_users();
    }
    kick_user(username, "banned" + (reason.empty() ? "" : ": " + reason));
    std::string msg = "User " + username + " has been banned" + (reason.empty() ? "." : " reason: " + reason);
    if (until != PERMANENT && until != 0) msg += " Time left: " + time_left(until);
    std::cout << msg << std::endl;
    broadcast(msg + "\n", INVALID_SOCKET);
}

void mute_user(const std::string& username, uint64_t until, const std::string& reason) {
    {
        std::lock_guard<std::mutex> lock(db_mutex);
        auto it = user_db.find(username);
        if (it == user_db.end()) return;
        it->second.mute_until = until;
        it->second.mute_reason = reason;
        save_users();
    }
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = clients.find(username);
        if (it != clients.end()) {
            it->second.muted = true;
            std::string msg = "You have been muted" + (reason.empty() ? "" : " reason: " + reason);
            if (until != PERMANENT && until != 0) msg += " until " + time_left(until);
            send_to_client(it->second.socket, msg + "\n");
        }
    }
    std::string global = "User " + username + " has been muted" + (reason.empty() ? "." : " reason: " + reason);
    if (until != PERMANENT && until != 0) global += " Time left: " + time_left(until);
    std::cout << global << std::endl;
    broadcast(global + "\n", INVALID_SOCKET);
    log_message("Muted " + username);
}

void unban_user(const std::string& username) {
    {
        std::lock_guard<std::mutex> lock(db_mutex);
        auto it = user_db.find(username);
        if (it == user_db.end()) return;
        it->second.ban_until = 0;
        it->second.ban_reason = "";
        save_users();
    }
    std::string msg = "User " + username + " has been unbanned.";
    std::cout << msg << std::endl;
    broadcast(msg + "\n", INVALID_SOCKET);
}

void unmute_user(const std::string& username) {
    {
        std::lock_guard<std::mutex> lock(db_mutex);
        auto it = user_db.find(username);
        if (it == user_db.end()) return;
        it->second.mute_until = 0;
        it->second.mute_reason = "";
        save_users();
    }
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = clients.find(username);
        if (it != clients.end()) {
            it->second.muted = false;
            send_to_client(it->second.socket, "You have been unmuted. Feel free to write.\n");
        }
    }
    std::string msg = "User " + username + " has been unmuted.";
    std::cout << msg << std::endl;
    broadcast(msg + "\n", INVALID_SOCKET);
}

void change_role(const std::string& username, const std::string& new_role, SOCKET sender_socket) {
    {
        std::lock_guard<std::mutex> lock(db_mutex);
        auto it = user_db.find(username);
        if (it == user_db.end()) {
            send_to_client(sender_socket, "User not found.\n");
            return;
        }
        if (role_commands.find(new_role) == role_commands.end()) {
            send_to_client(sender_socket, "Role " + new_role + " does not exist.\n");
            return;
        }
        if (it->second.role == "Admin" && new_role != "Admin") {
            send_to_client(sender_socket, "Cannot demote another admin.\n");
            return;
        }
        it->second.role = new_role;
        save_users();
    }
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = clients.find(username);
        if (it != clients.end()) {
            it->second.role = new_role;
            std::string cmd_list = get_commands_for_role(new_role);
            send_to_client(it->second.socket, "ROLE_CHANGED " + new_role + " " + cmd_list + "\n");
        }
    }
    std::string msg = username + "'s role is now \"" + new_role + "\"";
    std::cout << msg << std::endl;
    broadcast(msg + "\n", INVALID_SOCKET);
    send_to_client(sender_socket, "Role changed.\n");
}

void check_expirations() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        uint64_t now = now_seconds();
        std::vector<std::string> to_unban, to_unmute;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            for (auto& p : user_db) {
                if (p.second.ban_until != PERMANENT && p.second.ban_until != 0 && p.second.ban_until <= now) {
                    p.second.ban_until = 0;
                    p.second.ban_reason = "";
                    to_unban.push_back(p.first);
                }
                if (p.second.mute_until != PERMANENT && p.second.mute_until != 0 && p.second.mute_until <= now) {
                    p.second.mute_until = 0;
                    p.second.mute_reason = "";
                    to_unmute.push_back(p.first);
                }
            }
            if (!to_unban.empty() || !to_unmute.empty()) save_users();
        }
        for (auto& u : to_unban) {
            std::string msg = "User " + u + " is no longer banned.";
            std::cout << msg << std::endl;
            broadcast(msg + "\n", INVALID_SOCKET);
            std::lock_guard<std::mutex> lock(clients_mutex);
            auto it = clients.find(u);
            if (it != clients.end())
                send_to_client(it->second.socket, "Your ban has expired. You may rejoin.\n");
        }
        for (auto& u : to_unmute) {
            std::string msg = "User " + u + " is no longer muted.";
            std::cout << msg << std::endl;
            broadcast(msg + "\n", INVALID_SOCKET);
            std::lock_guard<std::mutex> lock(clients_mutex);
            auto it = clients.find(u);
            if (it != clients.end()) {
                it->second.muted = false;
                send_to_client(it->second.socket, "Your mute has expired. You can talk now.\n");
            }
        }
    }
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
    if (username.length() == 0 || username.length() > 20) {
        send_to_client(client_socket, "Username must be 1-20 characters.\n");
        closesocket(client_socket);
        return;
    }
    bool new_user = false;
    std::string role = "Member";
    {
        std::lock_guard<std::mutex> lock(db_mutex);
        auto it = user_db.find(username);
        if (it == user_db.end()) {
            if (username == "admin") role = "Admin";
            user_db[username] = {"", role, 0, 0, "", ""};
            save_users();
            new_user = true;
        } else {
            if (it->second.ban_until != 0 && (it->second.ban_until == PERMANENT || it->second.ban_until > now_seconds())) {
                std::string msg = "YOU_ARE_BANNED";
                if (it->second.ban_until != PERMANENT && it->second.ban_until > 0)
                    msg += " for " + time_left(it->second.ban_until);
                if (!it->second.ban_reason.empty())
                    msg += " reason: " + it->second.ban_reason;
                send_to_client(client_socket, msg + "\n");
                closesocket(client_socket);
                return;
            }
            role = it->second.role;
        }
    }
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        if (clients.find(username) != clients.end()) {
            send_to_client(client_socket, "USERNAME_TAKEN\n");
            closesocket(client_socket);
            return;
        }
        clients[username] = {client_socket, username, role, false};
        auto it = user_db.find(username);
        if (it != user_db.end() && it->second.mute_until != 0 && (it->second.mute_until == PERMANENT || it->second.mute_until > now_seconds()))
            clients[username].muted = true;
    }
    std::string welcome = username + " joined the chat\n";
    if (new_user) {
        std::cout << "New user registered: " << username << std::endl;
    }
    std::cout << welcome;
    broadcast(welcome, client_socket);
    log_message(welcome);
    std::string cmd_list = get_commands_for_role(role);
    send_to_client(client_socket, "YOUR_ROLE " + role + " " + cmd_list + "\n");

    while (running) {
        bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        std::string message(buffer);
        if (!message.empty() && message.back() == '\n') message.pop_back();

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            auto it = clients.find(username);
            if (it == clients.end()) break;
            if (it->second.muted) {
                send_to_client(client_socket, "You are muted.\n");
                continue;
            }
        }

        if (message.substr(0, 5) == "/msg ") {
            size_t sp = message.find(' ', 5);
            if (sp != std::string::npos) {
                std::string target = message.substr(5, sp - 5);
                std::string pm_msg = message.substr(sp + 1);
                if (target == username) {
                    send_to_client(client_socket, "Cannot message yourself.\n");
                } else if (!send_private(target, pm_msg, client_socket, username)) {
                    send_to_client(client_socket, "User " + target + " offline.\n");
                } else {
                    log_message("[PM] " + username + " -> " + target + ": " + pm_msg);
                }
            } else {
                send_to_client(client_socket, "Usage: /msg <user> <msg>\n");
            }
        }
        else if (message.substr(0, 10) == "/broadcast " && role == "Admin") {
            std::string broadcast_msg = message.substr(10);
            if (!broadcast_msg.empty()) {
                std::string formatted = "[SERVER]: " + broadcast_msg + "\n";
                broadcast_all(formatted);
                log_message("[SERVER] " + broadcast_msg);
                std::cout << "Admin broadcast: " << broadcast_msg << std::endl;
                send_to_client(client_socket, "Broadcast sent.\n");
            } else {
                send_to_client(client_socket, "Usage: /broadcast <message>\n");
            }
        }
        else if (message == "/users") {
            std::string table = "\n";
            table += "Username            Role        Status      Ban Status          Mute Status\n";
            table += "-------------------- ----------- ----------- ------------------- -----------------\n";
            std::lock_guard<std::mutex> lock(db_mutex);
            for (auto& p : user_db) {
                bool online;
                {
                    std::lock_guard<std::mutex> lock2(clients_mutex);
                    online = (clients.find(p.first) != clients.end());
                }
                std::string status = online ? "online" : "offline";
                std::string ban_info;
                if (p.second.ban_until == PERMANENT) ban_info = "banned (perm)";
                else if (p.second.ban_until == 0) ban_info = "ok";
                else if (p.second.ban_until > now_seconds()) ban_info = "banned (" + time_left(p.second.ban_until) + ")";
                else ban_info = "ok";
                std::string mute_info;
                if (p.second.mute_until == PERMANENT) mute_info = "muted (perm)";
                else if (p.second.mute_until == 0) mute_info = "ok";
                else if (p.second.mute_until > now_seconds()) mute_info = "muted (" + time_left(p.second.mute_until) + ")";
                else mute_info = "ok";
                char line[128];
                snprintf(line, sizeof(line), "%-20s %-11s %-11s %-19s %s\n", p.first.c_str(), p.second.role.c_str(), status.c_str(), ban_info.c_str(), mute_info.c_str());
                table += line;
            }
            send_to_client(client_socket, table);
        }
        else if (message.substr(0, 5) == "/kick" && role == "Admin") {
            std::string rest = message.substr(6);
            size_t sp = rest.find(' ');
            std::string target = rest;
            std::string reason;
            if (sp != std::string::npos) {
                target = rest.substr(0, sp);
                reason = rest.substr(sp+1);
            }
            if (target.empty()) send_to_client(client_socket, "Usage: /kick <username> [reason]\n");
            else if (target == username) send_to_client(client_socket, "Cannot kick yourself.\n");
            else kick_user(target, reason);
        }
        else if (message.substr(0, 4) == "/ban" && role == "Admin") {
            std::string rest = message.substr(5);
            size_t sp1 = rest.find(' ');
            if (sp1 == std::string::npos) {
                send_to_client(client_socket, "Usage: /ban <username> <time> [reason]\n");
                continue;
            }
            std::string target = rest.substr(0, sp1);
            rest = rest.substr(sp1+1);
            size_t sp2 = rest.find(' ');
            std::string time_str;
            std::string reason;
            if (sp2 == std::string::npos) {
                time_str = rest;
            } else {
                time_str = rest.substr(0, sp2);
                reason = rest.substr(sp2+1);
            }
            if (target.empty() || time_str.empty()) {
                send_to_client(client_socket, "Usage: /ban <username> <time> [reason]\n");
                continue;
            }
            if (user_db.find(target) == user_db.end()) {
                send_to_client(client_socket, "User not found.\n");
                continue;
            }
            if (target == username) {
                send_to_client(client_socket, "Cannot ban yourself.\n");
                continue;
            }
            if (user_db[target].role == "Admin") {
                send_to_client(client_socket, "Cannot ban another admin.\n");
                continue;
            }
            uint64_t until = parse_time(time_str);
            if (until == 0) {
                send_to_client(client_socket, "Invalid time format.\n");
                continue;
            }
            ban_user(target, until, reason);
            send_to_client(client_socket, "User " + target + " banned.\n");
        }
        else if (message.substr(0, 7) == "/unban" && role == "Admin") {
            std::string target = message.substr(8);
            if (target.empty()) send_to_client(client_socket, "Usage: /unban <username>\n");
            else if (user_db.find(target) == user_db.end()) send_to_client(client_socket, "User not found.\n");
            else {
                unban_user(target);
                send_to_client(client_socket, "User " + target + " unbanned.\n");
            }
        }
        else if (message.substr(0, 5) == "/mute" && role == "Admin") {
            std::string rest = message.substr(6);
            size_t sp1 = rest.find(' ');
            if (sp1 == std::string::npos) {
                send_to_client(client_socket, "Usage: /mute <username> <time> [reason]\n");
                continue;
            }
            std::string target = rest.substr(0, sp1);
            rest = rest.substr(sp1+1);
            size_t sp2 = rest.find(' ');
            std::string time_str;
            std::string reason;
            if (sp2 == std::string::npos) {
                time_str = rest;
            } else {
                time_str = rest.substr(0, sp2);
                reason = rest.substr(sp2+1);
            }
            if (target.empty() || time_str.empty()) {
                send_to_client(client_socket, "Usage: /mute <username> <time> [reason]\n");
                continue;
            }
            if (user_db.find(target) == user_db.end()) {
                send_to_client(client_socket, "User not found.\n");
                continue;
            }
            if (target == username) {
                send_to_client(client_socket, "Cannot mute yourself.\n");
                continue;
            }
            if (user_db[target].role == "Admin") {
                send_to_client(client_socket, "Cannot mute another admin.\n");
                continue;
            }
            uint64_t until = parse_time(time_str);
            if (until == 0) {
                send_to_client(client_socket, "Invalid time format.\n");
                continue;
            }
            mute_user(target, until, reason);
            send_to_client(client_socket, "User " + target + " muted.\n");
        }
        else if (message.substr(0, 7) == "/unmute" && role == "Admin") {
            std::string target = message.substr(8);
            if (target.empty()) send_to_client(client_socket, "Usage: /unmute <username>\n");
            else if (user_db.find(target) == user_db.end()) send_to_client(client_socket, "User not found.\n");
            else {
                unmute_user(target);
                send_to_client(client_socket, "User " + target + " unmuted.\n");
            }
        }
        else if (message.substr(0, 3) == "/cr" && role == "Admin") {
            std::string rest = message.substr(4);
            size_t sp = rest.find(' ');
            if (sp == std::string::npos) {
                send_to_client(client_socket, "Usage: /cr <username> <role>\n");
                continue;
            }
            std::string target = rest.substr(0, sp);
            std::string new_role = rest.substr(sp+1);
            if (target.empty() || new_role.empty()) {
                send_to_client(client_socket, "Usage: /cr <username> <role>\n");
                continue;
            }
            if (target == username) {
                send_to_client(client_socket, "Cannot change your own role.\n");
                continue;
            }
            if (user_db.find(target) == user_db.end()) {
                send_to_client(client_socket, "User not found.\n");
                continue;
            }
            if (user_db[target].role == "Admin" && new_role != "Admin") {
                send_to_client(client_socket, "Cannot demote another admin.\n");
                continue;
            }
            change_role(target, new_role, client_socket);
        }
        else if (message == "/quit") {
            break;
        }
        else {
            std::string formatted = "[" + role + "] " + username + ": " + message + "\n";
            std::cout << formatted;
            broadcast_all(formatted);
            log_message("[PUB] " + username + ": " + message);
        }
    }
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(username);
    }
    std::string leave = username + " left the chat\n";
    std::cout << leave;
    broadcast(leave, INVALID_SOCKET);
    log_message(leave);
    closesocket(client_socket);
}

void close_all_clients() {
    std::vector<SOCKET> socks;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& p : clients) socks.push_back(p.second.socket);
        clients.clear();
    }
    for (SOCKET s : socks) {
        send_to_client(s, "SERVER_SHUTDOWN\n");
        shutdown(s, SD_BOTH);
        closesocket(s);
    }
}

BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        std::cout << "\nGracefully shutting down server...\n";
        running = false;
        if (server_fd != INVALID_SOCKET) {
            shutdown(server_fd, SD_BOTH);
            closesocket(server_fd);
            server_fd = INVALID_SOCKET;
        }
        return TRUE;
    }
    return FALSE;
}

void console_input() {
    std::string line;
    while (running) {
        if (!std::getline(std::cin, line)) break;
        if (!running) break;
        if (line.empty()) continue;
        std::string formatted = "[SERVER]: " + line + "\n";
        broadcast_all(formatted);
        log_message("[SERVER] " + line);
        std::cout << "Server message sent.\n";
    }
}

int main() {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    SetConsoleCtrlHandler(console_handler, TRUE);
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    load_roles();
    load_users();

    struct sockaddr_in address;
    int addrlen = sizeof(address);
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    if (listen(server_fd, 3) == SOCKET_ERROR) {
        std::cerr << "Listen failed\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    std::cout << "Server listening on port " << PORT << "\n";
    std::cout << "Type messages here to broadcast to all clients.\n";

    std::thread input_th(console_input);
    input_th.detach();
    std::thread exp_th(check_expirations);
    exp_th.detach();

    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        struct timeval tv = {1, 0};
        int sel = select(0, &readfds, nullptr, nullptr, &tv);
        if (sel == SOCKET_ERROR) {
            if (!running) break;
            continue;
        }
        if (sel == 0) continue;
        if (FD_ISSET(server_fd, &readfds)) {
            SOCKET client_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
            if (client_socket == INVALID_SOCKET) {
                if (!running) break;
                continue;
            }
            std::thread(handle_client, client_socket).detach();
            std::cout << "New client connected.\n";
        }
    }

    broadcast_all("SERVER_SHUTDOWN\n");
    close_all_clients();
    WSACleanup();
    if (logfile.is_open()) logfile.close();
    std::cout << "Server stopped.\n";
    return 0;
}
