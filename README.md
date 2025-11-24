# Multithread Chat Application

A feature-rich UDP-based chat application built in C with multithreading support. This project demonstrates concurrent programming concepts including thread management, synchronization primitives, client-server architecture, and ncurses UI development.

## Overview

This chat system consists of two main components:

- **Server** (`chat_server.c`): A multithreaded UDP server that manages connected clients, handles message routing, maintains user state, preserves message history, and monitors for inactive connections
- **Client** (`chat_client.c`): A UDP client with dual-threaded architecture featuring an ncurses-based interactive UI for simultaneous sending and receiving

The server uses read-write locks to safely manage a client table shared across multiple worker threads, while clients use separate threads for handling incoming messages and user input with a scrollable chat interface.

## Architecture

### Server Architecture

The server implements a **thread-per-request model with background monitoring**:

- **Main Loop**: Listens for incoming UDP packets and spawns a new worker thread for each request
- **Global Client Table**: Maintains an array of up to 128 connected clients with their names, addresses, mute lists, and activity tracking
- **Thread Safety**: All access to the client table is protected by a `pthread_rwlock_t` (read-write lock) to allow concurrent readers and exclusive writers
- **Worker Threads**: Each request is handled by a dedicated thread that processes commands, updates client state, and broadcasts messages
- **Message History**: Maintains a circular buffer of the last 15 messages sent to the chat
- **Inactivity Monitor**: Background thread that tracks client activity and removes inactive clients after 10 minutes
- **Ping System**: Sends keep-alive pings to inactive clients before removal; removes clients that don't respond within 20 seconds

### Client Architecture

The client uses a **dual-threaded approach with ncurses UI**:

- **Main Thread**: Initializes the UDP socket, ncurses interface, and creates two worker threads
- **Listener Thread**: Continuously reads incoming messages from the server and displays them in a scrollable chat window
- **Sender Thread**: Reads user input from the input line and sends commands to the server
# Multithread Chat Application

A feature-rich UDP-based chat application written in C. It demonstrates multithreading, synchronization primitives, a small command protocol, an ncurses-based client UI, message history, and time-based inactivity monitoring.

## Overview

This project contains two primary programs:

- `chat_server.c` — a multithreaded UDP server that manages client state, enforces name uniqueness, preserves a short message history, and monitors inactive clients.
- `chat_client.c` — an ncurses-backed client that runs two threads (listener + sender) for real-time send/receive and provides a scrollable chat UI. An `--admin` flag uses port `6666` and allows admin-only commands (e.g. `kick`).

There is also a convenience script `compile.sh` that stops running instances and builds both binaries.

## What changed (new/important behavior)

- Message history: the server now stores the last 15 messages in a circular buffer and sends them to newly connected clients. History messages are prefixed with `[History] ` so the client can handle/display them specially.
- Client timestamps: the ncurses client prepends a small timestamp (format `[HH:MM]`) to received messages and prints it right-aligned on the chat line.
- Inactivity monitoring: a background monitor thread pings the least-recently-active client after 10 minutes of inactivity; if the client doesn't respond within 20 seconds it is removed and others are notified.
- UDP sends: server `udp_socket_write()` calls now send only the used string length (i.e. `strlen(msg)`) rather than a fixed buffer size; this reduces unnecessary network traffic and avoids sending trailing garbage.
- Compile helper: `compile.sh` kills any running `chat_server`/`chat_client`, removes old binaries, and re-compiles (server + client). The client link command includes `-lncurses`.

## Command protocol

All protocol messages use the simple `type$payload` format. Example:

- `conn$Alice` — register the client name `Alice`
- `say$Hello` — broadcast `Hello` to other clients
- `sayto$Bob hi` — private message to `Bob` with payload `hi`
- `mute$SpamBot`, `unmute$SpamBot`, `rename$NewName`, `disconn$`

Internal messages:
- `ping$` — server → client (keepalive)
- `ret-ping$` — client → server (keeps client alive)

Server responses use prefixes:
- `SYS$...` — informational/system messages
- `ERR$...` — error messages

History messages are sent as: `[History] <original message>` so clients can optionally mark them.

## Important implementation notes

- The server stores client records with `last_active` timestamps and `ping_sent` flags. The monitor thread runs every `MONITOR_INTERVAL` seconds.
- The server's history buffer is protected by a mutex (`history_lock`) and is safe for concurrent writes from multiple worker threads.
- When broadcasting messages the server uses `udp_socket_write(sd, &addr, msg, strlen(msg))` (length = `strlen(msg)`).
- The client displays messages in a large `newpad()` buffer (5,000 lines by default) and keeps an input window at the bottom. Messages include a timestamp inserted by the client when displaying received data.

## Building

You can build manually or use the helper script.

Manual build:
```bash
gcc -o chat_server chat_server.c udp.c -lpthread
gcc -o chat_client chat_client.c udp.c -lpthread -lncurses
```

Using the helper script (recommended during development):
```bash
./compile.sh
```
`compile.sh` will `pkill` existing processes, remove previous binaries and rebuild.

## Running

Start the server (default port comes from `udp.h`):
```bash
./chat_server
```

Start a normal client:
```bash
./chat_client
```

Start an admin client (binds to port 6666 and may execute `kick$`):
```bash
./chat_client --admin
```

On connect, the client should send `conn$YourName` to register. After that the client can `say$...`, `sayto$...`, etc.

## UI controls (ncurses client)

- Up arrow / Down arrow: scroll chat history
- Enter: send message / command typed in the input line
- Backspace: delete character
- Ctrl+C: exit client (also `disconn$` to leave gracefully)

History messages are displayed as received; the client adds a `[HH:MM]` timestamp aligned to the right of each printed chat line.

## Files of interest

- `chat_server.c` — server implementation (worker threads, monitor thread, history, mute lists)
- `chat_client.c` — ncurses client (listener and sender threads, timestamping, scrollable pad)
- `udp.h` / `udp.c` — small wrapper for UDP socket ops (open/read/write)
- `compile.sh` — development helper script
- `test_scripts/` — small test utilities (e.g., `test_history.py`) used while developing features

## Configuration constants (from source)

- `HISTORY_SIZE = 15` — number of messages preserved
- `INACTIVITY_THRESHOLD = 600` — seconds (10 minutes)
- `PING_TIMEOUT = 20` — seconds to wait for ping response
- `MONITOR_INTERVAL = 10` — seconds between monitor checks
- `MAX_CLIENTS = 128`, `MAX_NAME_LEN = 64`, `MAX_MUTE = 64`

## Notes, caveats and next steps

- The server relies on `udp.h` for port and buffer-size constants; verify `SERVER_PORT` / `BUFFER_SIZE` there when deploying.
- Message framing is simple `\0` terminated strings — consider adding message length fields or structured frames if you need robustness across lossy networks.
- The client UI uses a fixed pad size (5000 lines); consider switching to a dynamic buffer or file-backed history for long-running sessions.

If you want, I can also:
- add a small `README` section describing the `udp.h` API used by these programs,
- add a small test harness to exercise history delivery (automated), or
- create a systemd unit / supervisord config for running the server persistently.
