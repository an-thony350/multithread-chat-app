# Multithreaded Chat Application — Implementation Notes

> This README documents how the current repository implements the assignment requirements.
> It focuses on design choices, data structures, threads, synchronization, and how features (including both PEs) are realized in the provided sources: `chat_server.c`, `chat_client.c`, and `udp.h`.

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
- [Usage examples and quick tests](#usage-examples-and-quick-tests)
- [Notes, caveats, and suggestions](#notes-caveats-and-suggestions)
- [Repository layout](#repository-layout)

---

## High-level summary
- The server is UDP-based, listens on `SERVER_PORT` (12000 by assignment), uses a fixed-size client table (`MAX_CLIENTS`) and spawns a detached worker thread per incoming request.
- The client is a terminal UI implemented with `ncurses`, spawns one sender thread and one listener thread, and sends raw requests like `type$payload` to the server.
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
- Storage: fixed-size array `clients[MAX_CLIENTS]` (simple contiguous table).
  - Rationale: simple and deterministic. Table entry indices are used as client IDs internally.

### Networking and worker model
- Main listener loop:
  - Calls `udp_socket_read(sd, &client_addr, buf, BUFFER_SIZE)` to receive a datagram.
  - For every valid incoming datagram, it allocates a `worker_arg_t`, copies the request and the client's address, and spawns a detached thread that runs `request_handler`.
- `request_handler`:
  - Parses messages of form `type$payload` (looks for `$` separator).
  - Recognized types: `conn`, `say`, `sayto`, `mute`, `unmute`, `rename`, `disconn`, `kick`, and `ret-ping` (the ping reply handling is minimal).
  - For malformed or unknown commands it replies with an `ERR$`-prefixed message.

### Broadcasting, private messages, and mute handling
- `broadcast_all(sd, msg, skip_idx)`:
  - Writes the message to history then iterates active clients and uses `udp_socket_write` to send to each (skips `skip_idx` when provided).
- `broadcast_from_sender(sd, sender_idx, msg)`:
  - Respects recipients' mute lists by calling `recipient_has_muted_sender`.
  - Does not deliver a sender's message to themselves.
- Private messaging (`sayto`):
  - The server parses the first token of `payload` as recipient name, finds recipient index via `find_client_by_name`, checks if recipient muted the sender, then sends a direct UDP message to the recipient and a `SYS$` ack to the sender.

### History buffer (PE 1)
- Circular buffer implemented with:
  - `history[HISTORY_SIZE][BUFFER_SIZE]`
  - `history_start`, `history_count`
  - Protected by `history_lock` (a pthread mutex).
- On every broadcast, `history_add(msg)` appends to the circular buffer.
- On successful `conn$` the server calls `history_send_to_client(sd, &client_addr)` to send each stored message prefixed with `[History] ` so the client can detect it and display accordingly.

### Inactivity monitor (PE 2)
- Monitor thread (`monitor_thread`) runs periodically (interval `MONITOR_INTERVAL` seconds).
- It finds the least-recently-active client by scanning `clients[]` under a read lock.
- If inactivity exceeds `INACTIVITY_THRESHOLD`, it:
  - Sends a `ping$` message to the selected client and marks `ping_sent` and `ping_time`.
  - If a previous ping exists and `PING_TIMEOUT` elapses without activity (clients update `last_active` when they send any request), the monitor removes the client and broadcasts a disconnection message.
- Ping replies: clients should reply with `ret-ping$` (the server accepts `ret-ping` but request handler currently returns early on `ret-ping` — presence of `ret-ping` resets last-active logic when the client is found).

### Synchronization strategy
- `clients_lock` is a `pthread_rwlock_t`:
  - Read lock (`pthread_rwlock_rdlock`) used for operations that only inspect the table (e.g., scanning for recipients for `sayto`, broadcasting).
  - Write lock (`pthread_rwlock_wrlock`) used for modifications: `add_client`, `remove_client_by_index`, muting/unmuting, rename, updating `last_active`, and marking ping fields.
- `history_lock` is a `pthread_mutex_t` guarding the circular history buffer.
- Rationale: Reader–writer lock allows multiple concurrent reads (e.g., many broadcast/send ops) while serializing updates.

### Admin / kick
- Admin client detection: server checks requester UDP port (`ntohs(client_addr.sin_port)`) and only accepts `kick$` from port `6666`.
- Upon `kick$ name`, server:
  - Finds target by name.
  - Sends `SYS$You have been removed from the chat` to the target.
  - Removes client from table and broadcasts `SYS$<name> has been removed...` to others.

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
gcc chat_server.c -o chat_server -lpthread
gcc chat_client.c -o chat_client -lncurses
```

- Note: both binaries also require `udp.h` at compile time (it's included by both sources).
- Run server (foreground):

```bash
./chat_server
```

- Or background:

```bash
./chat_server &
# record PID and kill when done:
kill <PID>
```

- Run client (normal user):

```bash
./chat_client
```

- Run client (admin port 6666) to issue `kick$`:

```bash
./chat_client --admin
```

Troubleshooting:
- If `ncurses` not installed, install it via your package manager (e.g., `sudo apt-get install libncurses5-dev libncursesw5-dev` on Debian/Ubuntu).
- If ports are already in use, change `CLIENT_PORT` or ensure no other process bound to the same port.

---

## Usage examples and how the protocol looks on-the-wire
- Client sends plaintext UDP payloads of the form `type$payload`.
- Examples:
  - Connect: `conn$Alice`
  - Broadcast: `say$Hello everyone!`
  - Private: `sayto$Bob How are you?`
  - Mute: `mute$Charlie`
  - Unmute: `unmute$Charlie`
  - Rename: `rename$Alice123`
  - Disconnect: `disconn$`
  - Admin kick: `kick$Bob` (only from port `6666`)
- Server replies use simple prefixes:
  - `SYS$` for system messages
  - `ERR$` for errors
  - Regular chat lines are plain text like `Alice: Hello everyone!`

Quick test flow:
1. Start server in one terminal: `./chat_server`
2. Start client A: `./chat_client`
3. Type: `conn$Alice` in client A.
4. Start client B: `./chat_client`
5. Type: `conn$Bob` in client B.
6. From A: `say$Hi Bob!`
7. From B: `sayto$Alice I see you` — server will deliver to Alice only.

History test:
- If several `say$` messages were already broadcast, starting a new client and sending `conn$NewUser` will cause the server to send the last up to 15 broadcast messages prefixed with `[History]`.

Inactivity test:
- If a client is idle for longer than `INACTIVITY_THRESHOLD`, the server will attempt a `ping$`. If no reply is received within `PING_TIMEOUT` seconds, the server removes the client.

---

## Notes, caveats, and suggestions
- UDP is used: packets may be lost or reordered. The server and client don't implement retransmission. For a production-grade system either implement ack/retry or switch to TCP.
- The client currently binds to a fixed `CLIENT_PORT` (55555) unless `--admin` is used. For running multiple normal clients on same machine you will need to modify client code to use ephemeral ports (or allow port 0 and read assigned port), or run clients on separate machines.
- `chat_pad` uses a fixed size (5000 lines). That is simple and acceptable for the assignment, but could be turned into dynamic allocation for long-running sessions.
- Name uniqueness is enforced server-side: `conn$` fails if the requested name is already in use.
- `ret-ping` handling in the server is minimal: the presence of `ret-ping` is recognized and ignored in `request_handler` (but the server updates `last_active` upon receiving any message from a known client, which implicitly handles ping replies).
- Testing scripts in `test_scripts/` (if present) can be used to validate baseline behaviors — inspect them and adapt compilation flags if needed.

---

## Repository layout
- `chat_server.c` — server (listener, worker threads, monitor thread, client table, history)
- `chat_client.c` — client (ncurses UI, sender/listener threads)
- `udp.h` — UDP helper wrappers and constants (used by both client and server)
- `compile.sh` — optional helper script (if present)
- `test_scripts/` — automated tests (optional)

---


