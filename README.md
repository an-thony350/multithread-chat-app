# Multithread Chat Application

A UDP-based chat application built in C with multithreading support. This project demonstrates concurrent programming concepts including thread management, synchronization primitives, and client-server architecture.

## Overview

This chat system consists of two main components:

- **Server** (`chat_server.c`): A multithreaded UDP server that manages connected clients, handles message routing, and maintains user state
- **Client** (`chat_client.c`): A UDP client with dual-threaded architecture for simultaneous sending and receiving

The server uses read-write locks to safely manage a client table shared across multiple worker threads, while clients use separate threads for handling incoming messages and user input.

## Architecture

### Server Architecture

The server implements a **thread-per-request model**:

- **Main Loop**: Listens for incoming UDP packets and spawns a new worker thread for each request
- **Global Client Table**: Maintains an array of up to 128 connected clients with their names, addresses, and mute lists
- **Thread Safety**: All access to the client table is protected by a `pthread_rwlock_t` (read-write lock) to allow concurrent readers and exclusive writers
- **Worker Threads**: Each request is handled by a dedicated thread that processes commands, updates client state, and broadcasts messages

### Client Architecture

The client uses a **dual-threaded approach**:

- **Main Thread**: Initializes the UDP socket and creates two worker threads
- **Listener Thread**: Continuously reads incoming messages from the server and displays them
- **Sender Thread**: Reads user input from stdin and sends commands to the server
- **Concurrent I/O**: Both threads run simultaneously, allowing the user to send commands while receiving messages

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
| **kick** | `kick$<name>` | Remove a user from the chat (admin only) |
| **disconn** | `disconn$` | Disconnect from the chat |

### Message Types

The server responds with prefixed messages:
- `SYS$` - System messages (connections, disconnections, confirmations)
- `ERR$` - Error messages (validation failures, permission issues)
- Standard format for chat messages: `<username>: <message>`
- Private messages: `<username> (private): <message>`

## Key Features

### Concurrency & Thread Safety

- **Read-Write Locks**: Allows multiple reader threads for lookups while ensuring exclusive access during modifications
- **Worker Thread Pool**: Each client request spawns a detached worker thread
- **Safe Data Access**: Client table access is always protected; names are copied before operations outside locks

### User Management

- **Unique Names**: Enforces unique usernames across all connected clients
- **Client Registration**: Tracks each client by IP address and port
- **Name Changes**: Allows users to rename themselves mid-session
- **Graceful Disconnection**: Cleans up client state and notifies other users

### Muting System

- **Per-User Mute Lists**: Each client maintains a list of muted users (up to 64)
- **Selective Broadcasting**: When sending public messages, the server respects mute lists
- **Private Message Blocking**: Muted users cannot send private messages to those who muted them
- **Transparent Muting**: Senders are notified when their private messages are blocked

### Admin Features

- **Port-Based Authentication**: Admin commands (like `kick`) are only accepted from port 6666
- **User Removal**: Administrators can forcibly remove users from the chat

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

### Synchronization Strategy

- **Read Lock**: Used when looking up clients by name or address (frequent, non-blocking operations)
- **Write Lock**: Used when adding/removing clients, modifying mute lists, or changing names
- **Lock Scope**: Locks are acquired for minimal duration and always released before sending responses

### String Handling

- Safe string operations using `strncpy()` with null-termination
- Input parsing with `strtok()` for command tokenization
- String trimming utilities to handle leading/trailing whitespace and newlines

## Limits & Configuration

```c
#define MAX_CLIENTS 128         // Maximum concurrent users
#define MAX_NAME_LEN 64         // Maximum username length
#define MAX_MUTE 64             // Maximum mutes per user
#define CLIENT_PORT 55555       // Client-side UDP port
#define SERVER_PORT (defined in udp.h)  // Server-side UDP port
#define BUFFER_SIZE (defined in udp.h)  // Max message size
```

## Error Handling

- **Validation**: Empty names, duplicate usernames, and missing recipients are rejected
- **Permission Checks**: Only admin port can execute privileged commands
- **Graceful Degradation**: Malformed requests return descriptive error messages
- **Thread Safety**: All errors maintain consistent client table state

## Dependencies

- **POSIX Threads**: `pthread.h` for multithreading and synchronization
- **UDP Library**: `udp.h` for socket operations (`udp_socket_open`, `udp_socket_read`, `udp_socket_write`)
- **System Headers**: Standard C library (`stdio.h`, `stdlib.h`, `string.h`, etc.)
- **Network Headers**: `arpa/inet.h`, `netinet/in.h` for network address structures

## Compilation

```bash
gcc -o chat_server chat_server.c udp.c -lpthread
gcc -o chat_client chat_client.c udp.c -lpthread
```

## Usage

**Start the server**:
```bash
./chat_server
```

**Connect a client**:
```bash
./chat_client
```

**In the client, issue commands**:
```
conn$Alice
say$Hello everyone!
sayto$Bob This is a private message
mute$SpamBot
disconn$
```

## Learning Outcomes

This project demonstrates:
- Multithreaded programming with POSIX threads
- Thread synchronization using read-write locks
- Client-server architecture with UDP sockets
- Concurrent data structure management
- String parsing and command protocol design
- Network programming fundamentals
- Proper resource management and cleanup
