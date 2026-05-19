# Console Chat (C++ / Sockets)

A lightweight, multi‑client console chat application written in C++ using raw sockets.  
The server handles multiple clients concurrently via threads, and the client uses non‑blocking I/O for smooth typing while receiving messages.

## Features

- **Multi‑client support** – All connected users see each other’s messages in real time.
- **Graceful shutdown** – Press `Ctrl+C` on the server; all clients are notified and exit cleanly.
- **Cross‑platform** – Separate implementations for POSIX (Linux/macOS) and Windows (Winsock2).
- **Non‑blocking client input** – You can type messages while receiving incoming messages (no separate thread for sending).
- **No external dependencies** – Uses only standard and system libraries.

## Build & Run

```bash
git clone https://github.com/ha1ron23/console-chat-cpp.git
cd console-chat-cpp
```

### Linux / macOS

```bash
cd linux # or cd macOS
g++ -std=c++11 -pthread server.cpp -o server
g++ -std=c++11 -pthread client.cpp -o client

# Terminal 1: server
./server

# Terminal 2: client
./client
```

### Windows

```bash
cd windows
g++ -std=c++11 server.cpp -o server.exe -lws2_32 -pthread
g++ -std=c++11 client.cpp -o client.exe -lws2_32

# Run server.exe // on terminal 1
# Run client.exe // on terminal 2
```
**Note**: On Windows, the client uses _kbhit() for non‑blocking input. The server uses select() with a timeout and handles CTRL_C_EVENT

## Usage
1. Start the server, it will listen on port 9034

2. Start one or more clients

3. Enter a username when prompted

4. Type messages and press Enter to send

5. Type /quit or CTRL + C to leave the chat

6. Press Ctrl+C on the server to gracefully shut down all connections

## Commands

| Command | Description |
|---------|-------------|
| `/msg <username> <message>` | Send a private message to a specific user |
| `/users` | Show list of online users |
| `/quit` | Leave the chat |

## Implementation Details

    Server:

        Listens on a socket, accepts new clients.

        Spawns a thread per client to handle incoming messages.

        Broadcasts every message to all other clients.

        On Ctrl+C, sends SERVER_SHUTDOWN to all clients and closes sockets.

    Client (Linux):

        Uses select() to monitor both stdin and the socket.

        If the server disconnects, the client exits immediately.

    Client (Windows):

        Uses _kbhit() to check for keyboard input without blocking.

        The socket is set to non‑blocking mode temporarily to check for incoming data.

        If the server disconnects, the client exits automatically.

##  License

MIT License – see LICENSE file.

Contributing

Feel free to open issues or pull requests. Possible improvements:

    Add private messaging.

    Encrypt messages.

    Save chat history.
