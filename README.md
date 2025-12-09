# Multithreaded Chat Application

Anthony Bartlett - 0255 6059
Denzil Erza-Essien - 0259 3040

## Contents
- [High-level summary](#high-level-summary)
- [Server implementation (`chat_server.c`)](#server-implementation-chat_serverc)
  - [Client record and storage](#client-record-and-storage)
  - [Networking and worker model](#networking-and-worker-model)
  - [Broadcasting, private messages, and mute handling](#broadcasting-private-messages-and-mute-handling)
  - [History buffer (PE 1)](#history-buffer-pe-1)
  - [Inactivity monitor (PE 2)](#inactivity-monitor-pe-2)
  - [Synchronization strategy](#synchronization-strategy)
  - [Admin/kick behavior](#adminkick-behavior)
- [Client implementation (`chat_client.c`)](#client-implementation-chat_clientc)
  - [UI and threads](#ui-and-threads)
  - [Message handling and history markers](#message-handling-and-history-markers)
  - [Admin mode and ports](#admin-mode-and-ports)
- [Build & Run (exact flags used)](#build--run-exact-flags-used)
- [Supported Commands](#supported-commands)
- [Testing](#testing)
- [Repository layout](#repository-layout)

---

## High-level summary
- The server is UDP-based, listens on `SERVER_PORT`, stores connected clients in a linked list, and spawns a detached worker thread per incoming request.
- The client is a terminal UI implemented with `ncurses`, spawns one sender thread and one listener thread, and sends raw requests like `type$ payload` to the server.
- Two proposed extensions are implemented:
  - PE1: A circular history buffer of the last 15 broadcast messages; history is sent to new clients on `conn$`.
  - PE2: An inactivity monitor thread periodically pings the least-recently-active client and removes it if it does not respond.

---

## Server implementation (`chat_server.c`)

### Client record and storage
- Type: `Client` struct holds:
  - `active` flag
  - `name` (string)
  - `addr` (struct sockaddr_in)
  - `muted` list (array of strings) and `muted_count`
  - `last_active` timestamp (time_t)
  - `ping_sent` and `ping_time` for inactivity handling
- Each client is wrapped in a `ClientNode` and stored in a dynamically allocated singly linked list 
  ```c
  typedef struct ClientNode {
    Client client;
    struct ClientNode *next;
  } ClientNode;
  ```
- The head pointer `static ClientNode *clients_head;` represents the start of the client list
- Alinked list is used due to its dynamic size, having no wasted memory, or any client limits.

### Networking and worker model
- Main listener loop:
  - Calls `udp_socket_read(sd, &client_addr, buf, BUFFER_SIZE)` to receive a datagram.
  - For every valid incoming datagram, it allocates a `worker_arg_t`, copies the request and the client's address, and spawns a detached thread that runs `request_handler`.
- `request_handler`:
  - Parses messages of form `type$payload` (looks for `$` delimiter).
  - Recognized types: `conn`, `say`, `sayto`, `mute`, `unmute`, `rename`, `disconn`, `kick`, and `ret-ping` (the ping reply handling is minimal).
  - For malformed or unknown commands it replies with an `ERR$`-prefixed message.

### Broadcasting, private messages, and mute handling
- `broadcast_all(sd, msg, skip_idx)`:
  - Writes the message to history then iterates active clients and uses `udp_socket_write` to send to each (skips `skip_idx` when provided).
- `broadcast_from_sender(sd, sender_idx, msg)`:
  - Respects recipients' mute lists by calling `recipient_has_muted_sender`.
  - Does not deliver a sender's message to themselves.
- Private messaging (`sayto`):
  - The server parses the first token of `payload` as recipient name, finds recipient on the linked list via `find_client_by_name`, checks if recipient muted the sender, then sends a direct UDP message to the recipient and a `SYS$` ack to the sender.

### History buffer (PE 1)
- Circular buffer implemented with:
  - `history[HISTORY_SIZE][BUFFER_SIZE]`
  - `history_start`, `history_count`
  - Protected by `history_lock` (a pthread mutex).
- On every broadcast, `history_add(msg)` appends to the circular buffer.
- On successful `conn$` the server calls `history_send_to_client(sd, &client_addr)` to send each stored message prefixed with `[History] ` so the client can detect it and display accordingly.

### Inactivity monitor (PE 2)
- Monitor thread (`monitor_thread`) runs periodically (interval `MONITOR_INTERVAL` seconds).
- It finds the least-recently-active client by scanning the linked list under a read lock.
- If inactivity exceeds `INACTIVITY_THRESHOLD`, it:
  - Sends a `ping$` message to the selected client and marks `ping_sent` and `ping_time`.
  - If a previous ping exists and `PING_TIMEOUT` elapses without activity (clients update `last_active` when they send any request), the monitor removes the client and broadcasts a disconnection message.
- Ping replies: clients reply with `ret-ping$`.
- On receiving `ret-ping`, the server immediately updates:
  - `last_active`
  - `ping_sent = 0`
  - `ping_time = 0`
This prevents the client from being removed by the inactivity monitor thread.


### Synchronization strategy
- `clients_lock` is a `pthread_rwlock_t` protecting the linked list:
  - Read lock (`pthread_rwlock_rdlock`) used for operations that only inspect the linked list (e.g., scanning for recipients for `sayto`, broadcasting).
  - Write lock (`pthread_rwlock_wrlock`) used for modifications: `add_client`, `remove_client`, muting/unmuting, rename, updating `last_active`, and marking ping fields.
- `history_lock` is a `pthread_mutex_t` guarding the circular history buffer.
- Rationale: Reader–writer lock allows multiple concurrent reads (e.g., many broadcast/send ops) while serializing updates.

### Admin / kick
- Admin client detection: server checks requester UDP port (`ntohs(client_addr.sin_port)`) and only accepts `kick$` from port `6666`.
- Upon `kick$ name`, server:
  - Finds target by name.
  - Sends `SYS$You have been removed from the chat` to the target.
  - Removes client from linked list and broadcasts `SYS$<name> has been removed...` to others.

---

## Client implementation (`chat_client.c`)

### UI and threads
- Uses `ncurses` for terminal UI:
  - `chat_pad`: large pad used as the scrollable message area (created with `newpad(5000, cols)`).
  - `input_win`: single-line input window at the bottom for user typing.
- Threads:
  - `listener_thread(int *sd)`:
    - Blocks on `udp_socket_read` to receive server messages.
    - Adds each incoming line to `chat_pad` and stamps a local timestamp.
    - Recognizes messages prefixed with `[History]` and handles them appropriately (keeps them in the pad like normal messages).
  - `sender_thread(struct sender_args *args)`:
    - Reads user input using `wgetch` on `input_win` and builds a request string.
    - Sends the raw request string to server with `udp_socket_write`.
    - Special behavior: when user types `disconn$` the sender sets `should_exit = 1` and returns, letting the program terminate.
- Keyboard handling:
  - Supports up/down keys to scroll chat pad (`KEY_UP`, `KEY_DOWN`).
  - Handles backspace and line editing roughly (manual handling in loop).
- Cursor and refresh logic ensure the pad and input area don't overlap.

### Message handling and history markers
- On listening, when a message begins with "[History]", the client strips that marker for display (server prefixes history with `[History] ` when sending to newly connected clients).
- Timestamps are added on the client side (the client formats a local time `[HH:MM]` and prints it right-aligned on the pad).

### Admin mode and ports
- By default the client binds to `CLIENT_PORT` defined as `55555` in the source.
- Admin mode: if launched with `--admin` argument (`./chat_client --admin`) the client binds to port `6666`. This allows sending `kick$` commands accepted by the server as admin requests.

---

## Build & Run (exact flags used)
- The server uses pthreads; the client uses `ncurses`. Build commands:

```bash
# From repository root (multithread-chat-app/)
gcc chat_server.c -o chat_server 
gcc chat_client.c -o chat_client -lncurses -lpthread
```

- Run server (foreground):

```bash
./chat_server
```

- Or background:

```bash
./chat_server &
# kill when done:
pkill chat_server
```

- Run client (normal user):

```bash
./chat_client
```

- Run client (admin port 6666) to issue `kick$`:

```bash
./chat_client --admin
```

---
## Supported Commands

| Command               | Description                               |
|-----------------------|-------------------------------------------|
| conn$ NAME            | Connect to the chat with a username       |
| say$ MESSAGE          | Broadcast message                         |
| sayto$ USER MESSAGE   | Private message                           |
| mute$ USER            | Mute user                                 |
| unmute$ USER          | Unmute user                               |
| rename$ NEWNAME       | Change username                           |
| disconn$              | Disconnect                                |
| kick$ USER            | Admin command (port 6666 only)            |
| ret-ping$             | Client heartbeat reply                    |

---
## Testing

Automated test scripts are included under `test_scripts/` covering:

- Basic client-server communication
- Broadcast testing
- History buffer correctness
- Inactivity detection and removal
- Multi-client behaviour
- Stress/load testing

All scripts can be run individually:

```bash
python3 test_basics.py
python3 test_history.py
python3 test_inactivity.py
python3 test_multiclient.py
python3 test_stress.py
python3 test_stress_valid.py
python3 test_malformed
```

---
## Repository layout
- `chat_server.c` — server (listener, worker threads, monitor thread, client table, history)
- `chat_client.c` — client (ncurses UI, sender/listener threads)
- `udp.h` — UDP helper wrappers and constants (used by both client and server)
- `compile.sh` — compilation script 
- `test_scripts/` — automated tests to show functionality

---


