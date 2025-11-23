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
- **UI Features**: 
  - Scrollable chat history (up/down arrow keys)
  - Input prompt at the bottom of the screen
  - Separator line between chat and input areas
  - Real-time message display
- **Admin Mode**: Optional `--admin` flag allows connecting from port 6666 with admin privileges

## Command Protocol

All commands follow the format: `command$payload`

### Available Commands

| Command | Format | Description |
|---------|--------|-------------|
| **conn** | `conn$<name>` | Connect to the chat with a chosen username |
| **say** | `say$<message>` | Broadcast a message to all connected clients |
| **sayto** | `sayto$<recipient> <message>` | Send a private message to a specific client |
| **mute** | `mute$<name>` | Mute messages from a specific user |
| **unmute** | `unmute$<name>` | Unmute a previously muted user |
| **rename** | `rename$<new_name>` | Change your username |
| **kick** | `kick$<name>` | Remove a user from the chat (admin only, port 6666) |
| **disconn** | `disconn$` | Disconnect from the chat |
| **ping** | (internal) | Keep-alive signal from server to detect inactive clients |

### Message Types

The server responds with prefixed messages:
- `SYS$` - System messages (connections, disconnections, confirmations, inactivity notifications)
- `ERR$` - Error messages (validation failures, permission issues)
- Standard format for chat messages: `<username>: <message>`
- Private messages: `<username> (private): <message>`

## Key Features

### Concurrency & Thread Safety

- **Read-Write Locks**: Allows multiple reader threads for lookups while ensuring exclusive access during modifications
- **Worker Thread Pool**: Each client request spawns a detached worker thread
- **Safe Data Access**: Client table access is always protected; names are copied before operations outside locks
- **Mutex-Protected History**: Message history buffer is protected by a separate mutex for thread-safe access

### User Management

- **Unique Names**: Enforces unique usernames across all connected clients
- **Client Registration**: Tracks each client by IP address and port
- **Name Changes**: Allows users to rename themselves mid-session
- **Graceful Disconnection**: Cleans up client state and notifies other users
- **Activity Tracking**: Records last activity time for each client

### Muting System

- **Per-User Mute Lists**: Each client maintains a list of muted users (up to 64)
- **Selective Broadcasting**: When sending public messages, the server respects mute lists
- **Private Message Blocking**: Muted users cannot send private messages to those who muted them
- **Transparent Muting**: Senders are notified when their private messages are blocked

### Message History

- **Circular Buffer**: Stores the last 15 messages sent to the chat
- **Automatic Preservation**: All public and private messages are recorded
- **New Client Welcome**: When a client connects, they receive the last 15 messages automatically
- **Thread-Safe Access**: History is protected by a mutex for concurrent access

### Inactivity Monitoring

- **Automatic Timeout**: Clients inactive for 10 minutes are flagged
- **Keep-Alive Pings**: Server sends ping requests to inactive clients
- **Graceful Removal**: Clients that don't respond to pings within 20 seconds are removed
- **System Notifications**: All other clients are notified when someone is removed due to inactivity
- **Background Monitor**: Dedicated thread runs every 10 seconds to check and manage inactive clients

### Admin Features

- **Port-Based Authentication**: Admin commands (like `kick`) are only accepted from port 6666
- **User Removal**: Administrators can forcibly remove users from the chat
- **Special Client Mode**: Connect with `./chat_client --admin` to operate as admin

### User Interface (Client)

- **Scrollable Chat Window**: View message history with up/down arrow keys
- **Live Input Line**: Type commands and messages at the bottom
- **Visual Separator**: Clear division between chat area and input area
- **Responsive Design**: Handles terminal resizing gracefully
- **ncurses Integration**: Professional terminal UI with proper cursor management

## Technical Details

### Data Structures

**Client Record**:
```c
typedef struct {
    int active;                         // Is this slot in use?
    char name[MAX_NAME_LEN];            // Username
    struct sockaddr_in addr;            // Client network address (IP + port)
    char muted[MAX_MUTE][MAX_NAME_LEN]; // Mute list
    int muted_count;                    // Current mute count
    time_t last_active;                 // Last activity timestamp
    int ping_sent;                      // Has ping been sent?
    time_t ping_time;                   // When was ping sent?
} Client;
```

**Worker Arguments**:
```c
typedef struct {
    struct sockaddr_in client_addr;     // Client address
    char request[BUFFER_SIZE];          // Request payload
    int sd;                             // Socket descriptor
} worker_arg_t;
```

**Message History**:
```c
static char history[HISTORY_SIZE][BUFFER_SIZE];  // Circular message buffer
static int history_count;                        // Current message count
static int history_start;                        // Start index of circular buffer
static pthread_mutex_t history_lock;             // Thread safety
```

### Synchronization Strategy

- **Read Lock**: Used when looking up clients by name or address (frequent, non-blocking operations)
- **Write Lock**: Used when adding/removing clients, modifying mute lists, or changing names
- **History Mutex**: Protects read/write access to the message history buffer
- **Lock Scope**: Locks are acquired for minimal duration and always released before sending responses

### String Handling

- Safe string operations using `strncpy()` with null-termination
- Input parsing with `strtok()` for command tokenization
- String trimming utilities to handle leading/trailing whitespace and newlines

### Inactivity Monitoring Algorithm

```
Every 10 seconds:
1. Find the least recently active connected client
2. Check if (current_time - last_active) >= 600 seconds
3. If yes and no ping sent:
   - Mark ping_sent = 1
   - Record ping_time
   - Send "ping$" message to client
4. If ping was sent and (current_time - ping_time) >= 20 seconds:
   - Remove client from table
   - Notify client and broadcast to others
```

## Limits & Configuration

```c
#define MAX_CLIENTS 128              // Maximum concurrent users
#define MAX_NAME_LEN 64              // Maximum username length
#define MAX_MUTE 64                  // Maximum mutes per user
#define HISTORY_SIZE 15              // Messages to preserve
#define INACTIVITY_THRESHOLD 600     // Seconds (10 minutes)
#define PING_TIMEOUT 20              // Seconds to wait for ping response
#define MONITOR_INTERVAL 10          // Seconds between inactivity checks
#define CLIENT_PORT 55555            // Client-side UDP port (normal mode)
#define SERVER_PORT (defined in udp.h)  // Server-side UDP port
#define BUFFER_SIZE (defined in udp.h)  // Max message size
```

## Error Handling

- **Validation**: Empty names, duplicate usernames, and missing recipients are rejected
- **Permission Checks**: Only admin port can execute privileged commands
- **Graceful Degradation**: Malformed requests return descriptive error messages
- **Thread Safety**: All errors maintain consistent client table state
- **Timeout Handling**: Inactive clients are cleanly removed without corrupting shared state

## Dependencies

- **POSIX Threads**: `pthread.h` for multithreading and synchronization
- **ncurses Library**: `ncurses.h` for terminal UI (client only)
- **UDP Library**: `udp.h` for socket operations (`udp_socket_open`, `udp_socket_read`, `udp_socket_write`)
- **System Headers**: Standard C library (`stdio.h`, `stdlib.h`, `string.h`, `time.h`, etc.)
- **Network Headers**: `arpa/inet.h`, `netinet/in.h` for network address structures

## Compilation

```bash
# Server (no special libraries beyond POSIX)
gcc -o chat_server chat_server.c udp.c -lpthread

# Client (requires ncurses library)
gcc -o chat_client chat_client.c udp.c -lpthread -lncurses
```

## Usage

**Start the server**:
```bash
./chat_server
```

**Connect as a regular client**:
```bash
./chat_client
```

**Connect as an admin client** (can use `kick` command):
```bash
./chat_client --admin
```

**In the client, issue commands**:
```
conn$Alice
say$Hello everyone!
sayto$Bob This is a private message
mute$SpamBot
rename$Alice_v2
unmute$SpamBot
disconn$
```

**Admin commands** (from port 6666 only):
```
kick$Bob
```

**UI Navigation**:
- **Up Arrow**: Scroll up in chat history
- **Down Arrow**: Scroll down in chat history
- **Enter**: Send message/command
- **Backspace**: Delete character in input
- **Ctrl+C**: Exit client

## Learning Outcomes

This project demonstrates:
- Multithreaded programming with POSIX threads
- Thread synchronization using read-write locks and mutexes
- Client-server architecture with UDP sockets
- Concurrent data structure management
- String parsing and command protocol design
- Network programming fundamentals
- Proper resource management and cleanup
- Terminal UI development with ncurses
- Time-based monitoring and timeouts
- Circular buffer data structures
- Activity tracking and session management
