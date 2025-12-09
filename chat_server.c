/*
  Multithreaded UDP Chat Server
  -----------------------------
  Responsibilities:
    - Maintain a list of active clients (IP, port, name)
    - Handle chat commands (conn, say, sayto, mute, rename, kick, etc.)
    - Broadcast messages while respecting mute lists
    - Maintain a rolling history buffer for new connections
    - Detect and remove inactive clients via a monitor thread
 
  Concurrency Model:
    - Each incoming packet is handled by a detached worker thread.
    - Shared client table protected by a pthread rwlock:
        - Read lock for lookups / broadcasts
        - Write lock for mutations (connect, rename, mute, kick)
    - Chat history protected by a mutex.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include "udp.h"

#define MAX_NAME_LEN 64
#define MAX_MUTE 64
#define HISTORY_SIZE 15
#define INACTIVITY_THRESHOLD 10 // seconds
#define PING_TIMEOUT 20 // seconds
#define MONITOR_INTERVAL 10 // seconds

// Circular buffer storing the last HISTORY_SIZE broadcast messages
static char history[HISTORY_SIZE][BUFFER_SIZE];
static int history_count = 0;
static int history_start = 0;
static pthread_mutex_t history_lock = PTHREAD_MUTEX_INITIALIZER;

// Forward declarations
static void history_add(const char *msg);
static void history_send_to_client(int sd, struct sockaddr_in *addr);

// Representation of a connected client
// Maps an IP:port to a name and state
typedef struct {
    int active;                         
    char name[MAX_NAME_LEN];            
    struct sockaddr_in addr;             
    char muted[MAX_MUTE][MAX_NAME_LEN]; 
    int muted_count;

    time_t last_active;               
    int ping_sent;
    time_t ping_time;
} Client;

// Linked list node for clients
// Unordered and protected by clients_lock
typedef struct ClientNode {
    Client client;
    struct ClientNode *next;
} ClientNode;

// Head of linked list of clients 
static ClientNode *clients_head = NULL;
static pthread_rwlock_t clients_lock = PTHREAD_RWLOCK_INITIALIZER;

// UTILITY HELPERS 

//Compare two sockaddr_in for equality (as c doesn't let us use ==)
static int sockaddrs_equal(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return (a->sin_family == b->sin_family) &&
           (a->sin_port == b->sin_port) &&
           (a->sin_addr.s_addr == b->sin_addr.s_addr);
}

// Linear scan through linked list 
static ClientNode *find_client_by_addr(const struct sockaddr_in *addr) {
    ClientNode *cur = clients_head;
    while (cur) {
        if (cur->client.active && sockaddrs_equal(&cur->client.addr, addr))
            return cur;
        cur = cur->next;
    }
    return NULL;
}

// Find client node by name 
static ClientNode *find_client_by_name(const char *name) {
    ClientNode *cur = clients_head;
    while (cur) {
        if (cur->client.active && strcmp(cur->client.name, name) == 0)
            return cur;
        cur = cur->next;
    }
    return NULL;
}


// Add client to head of linked list
static ClientNode *add_client(const char *name, const struct sockaddr_in *addr) {
    if (find_client_by_name(name)) return NULL;

    ClientNode *node = malloc(sizeof(ClientNode));
    if (!node) return NULL;

    node->client.active = 1;
    strncpy(node->client.name, name, MAX_NAME_LEN - 1);
    node->client.name[MAX_NAME_LEN - 1] = '\0';

    node->client.addr = *addr;
    node->client.muted_count = 0;
    node->client.last_active = time(NULL);
    node->client.ping_sent = 0;
    node->client.ping_time = 0;

    node->next = clients_head;
    clients_head = node;

    return node;
}


// Remove client node from Linked List and free memory
static void remove_client(ClientNode *target) {
    if (!target) return;

    ClientNode **indirect = &clients_head;

    while (*indirect && *indirect != target)
        indirect = &(*indirect)->next;

    if (*indirect) {
        ClientNode *tmp = *indirect;
        *indirect = tmp->next;
        free(tmp);
    }
}


// Add name to client's muted list 
static int add_muted(ClientNode *client, const char *target) {
    if (!client) return -1;
    Client *c = &client->client;

    if (c->muted_count >= MAX_MUTE)
        return -1;

    // Prevent duplicates
    for (int i = 0; i < c->muted_count; ++i) {
        if (strcmp(c->muted[i], target) == 0)
            return 0;
    }

    strncpy(c->muted[c->muted_count], target, MAX_NAME_LEN - 1);
    c->muted[c->muted_count][MAX_NAME_LEN - 1] = '\0';
    c->muted_count++;

    return 0;
}


// Remove name from client's muted list 
static int remove_muted(ClientNode *client, const char *target) {
    if (!client) return -1;
    Client *c = &client->client;

    int found = -1;

    for (int i = 0; i < c->muted_count; ++i) {
        if (strcmp(c->muted[i], target) == 0) {
            found = i;
            break;
        }
    }

    if (found == -1)
        return -1;

    // Shift entries left
    for (int i = found; i < c->muted_count - 1; ++i) {
        strncpy(c->muted[i], c->muted[i + 1], MAX_NAME_LEN);
    }

    c->muted_count--;
    c->muted[c->muted_count][0] = '\0';

    return 0;
}


// Check if recipient has muted sender 
static int recipient_has_muted_sender(ClientNode *recipient, const char *sender_name) {
    if (!recipient) return 0;
    Client *c = &recipient->client;

    for (int i = 0; i < c->muted_count; ++i) {
        if (strcmp(c->muted[i], sender_name) == 0)
            return 1;
    }

    return 0;
}


// Broadcast message to all clients
// Writes to history 
static void broadcast_all(int sd, const char *msg, ClientNode *skip) {

    history_add(msg);

    pthread_rwlock_rdlock(&clients_lock);

    ClientNode *cur = clients_head;
    while (cur) {
        if (cur != skip && cur->client.active) {
            udp_socket_write(sd, &cur->client.addr, msg, strlen(msg));
        }
        cur = cur->next;
    }
    pthread_rwlock_unlock(&clients_lock);
}

// Broadcast sender's message respecting mute lists
static void broadcast_from_sender(int sd, ClientNode *sender, const char *msg) {
    if (!sender) return;

    history_add(msg);

    pthread_rwlock_rdlock(&clients_lock);

    ClientNode *cur = clients_head;
    while (cur) {
        if (cur != sender && cur->client.active &&
            !recipient_has_muted_sender(cur, sender->client.name)) {

            udp_socket_write(sd, &cur->client.addr, msg, strlen(msg));
        }
        cur = cur->next;
    }

    pthread_rwlock_unlock(&clients_lock);
}


// Append a message to history (thread-safe)
static void history_add(const char *msg) {
    pthread_mutex_lock(&history_lock);

    int index = (history_start + history_count) % HISTORY_SIZE;
    strncpy(history[index], msg, BUFFER_SIZE - 1);
    history[index][BUFFER_SIZE - 1] = '\0';

    if (history_count < HISTORY_SIZE) {
        history_count++;
    } else {
        // buffer full: overwrite oldest
        history_start = (history_start + 1) % HISTORY_SIZE;
    }

    pthread_mutex_unlock(&history_lock);
}

// Send last HISTORY_SIZE messages to a new client
static void history_send_to_client(int sd, struct sockaddr_in *addr) {
    pthread_mutex_lock(&history_lock);

    for (int i = 0; i < history_count; i++) {
        int index = (history_start + i) % HISTORY_SIZE;
        char wrapped[BUFFER_SIZE];
        snprintf(wrapped, BUFFER_SIZE, "[History] %s", history[index]);
        udp_socket_write(sd, addr, wrapped, strlen(wrapped)); 
    }

    pthread_mutex_unlock(&history_lock);
}


// Worker thread argument 
typedef struct {
    struct sockaddr_in client_addr;
    char request[BUFFER_SIZE];
    int sd;
} worker_arg_t;

// Trims trailing newline characters
static void rtrim(char *s) {
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) {
        s[len-1] = '\0';
        len--;
    }
}

// Trim leading and trailing spaces/tabs for safer parsing
static void trim_spaces(char *s) {
    // Trim leading
    char *start = s;
    while (*start == ' ' || *start == '\t')
        start++;

    if (start != s)
        memmove(s, start, strlen(start) + 1);

    // Trim trailing
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[len - 1] = '\0';
        len--;
    }
}

// Note: both fns trim_spaces and rtrim could be combined, but i kept separate for clarity


// Worker: handle one incoming request 
static void *request_handler(void *v) {
    worker_arg_t *arg = (worker_arg_t *)v;
    struct sockaddr_in client_addr = arg->client_addr;
    int sd = arg->sd;
    char buf[BUFFER_SIZE];
    strncpy(buf, arg->request, BUFFER_SIZE - 1);
    buf[BUFFER_SIZE - 1] = '\0';
    free(arg);

    rtrim(buf);

    // parse "type$payload"
    char *d = strchr(buf, '$');
    if (!d) {
        // malformed request
        char err[BUFFER_SIZE];
        snprintf(err, BUFFER_SIZE, "ERR$Malformed request (no $): %s\n", buf);
        udp_socket_write(sd, &client_addr, err, strlen(err));
        return NULL;
    }

    *d = '\0';
    char *type = buf;
    char *payload = d + 1;
    
    trim_spaces(type);
    trim_spaces(payload);

    // Respond to ret-ping immediately
    if (strcmp(type, "ret-ping") == 0) {
        pthread_rwlock_wrlock(&clients_lock);
        ClientNode *node = find_client_by_addr(&client_addr);
        if (node) {
            node->client.last_active = time(NULL);
            node->client.ping_sent = 0;
            node->client.ping_time = 0;
        }
        pthread_rwlock_unlock(&clients_lock);
        return NULL;
    }

    // Identify sender node 
    pthread_rwlock_rdlock(&clients_lock);
    ClientNode *sender = find_client_by_addr(&client_addr);
    pthread_rwlock_unlock(&clients_lock);

    if (sender) {
        // update last active time
        pthread_rwlock_wrlock(&clients_lock);
        sender->client.last_active = time(NULL);
        sender->client.ping_sent = 0;
        sender->client.ping_time = 0;
        pthread_rwlock_unlock(&clients_lock);
    }

    // Handle conn$name
    if (strcmp(type, "conn") == 0) {
        // payload = desired name
        if (payload == NULL || strlen(payload) == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$Name cannot be empty\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }

        // register under write lock
        pthread_rwlock_wrlock(&clients_lock);
        // check if already registered by address: update name if so
        ClientNode *existing = find_client_by_addr(&client_addr);

        if (existing) {
            if (find_client_by_name(payload) && strcmp(existing->client.name, payload) != 0) {
                pthread_rwlock_unlock(&clients_lock);
                char resp[BUFFER_SIZE];
                snprintf(resp, BUFFER_SIZE, "ERR$Name '%s' already in use\n", payload);
                udp_socket_write(sd, &client_addr, resp, strlen(resp));
                return NULL;
            }

            strncpy(existing->client.name, payload, MAX_NAME_LEN - 1);
            existing->client.name[MAX_NAME_LEN - 1] = '\0';
            sender = existing;
        } else {
            if (find_client_by_name(payload)) {
                pthread_rwlock_unlock(&clients_lock);
                char resp[BUFFER_SIZE];
                snprintf(resp, BUFFER_SIZE, "ERR$Name '%s' already in use\n", payload);
                udp_socket_write(sd, &client_addr, resp, strlen(resp));
                return NULL;
            }

            ClientNode *node = add_client(payload, &client_addr);
            if (!node) {
                pthread_rwlock_unlock(&clients_lock);
                char resp[BUFFER_SIZE];
                snprintf(resp, BUFFER_SIZE, "ERR$Server full or name taken\n");
                udp_socket_write(sd, &client_addr, resp, strlen(resp));
                return NULL;
            }

            sender = node;
        }

        pthread_rwlock_unlock(&clients_lock);

        // Send confirmation to new client
        char welcome[BUFFER_SIZE];
        snprintf(welcome, BUFFER_SIZE, "SYS$Hi %s, you have successfully connected to the chat\n", payload);
        udp_socket_write(sd, &client_addr, welcome, strlen(welcome));

        // Send last 15 messages to the new client
        history_send_to_client(sd, &client_addr);

        // Notify others
        char announce[BUFFER_SIZE];
        snprintf(announce, BUFFER_SIZE, "SYS$%s has joined the chat\n", payload);
        broadcast_all(sd, announce, sender); // skip sender (sender already got welcome)

        return NULL;
    }

    // Handle say$message (broadcast) 
    if (strcmp(type, "say") == 0) {

    if (!sender) {
        char resp[BUFFER_SIZE];
        snprintf(resp, BUFFER_SIZE,
                "ERR$You must conn$<name> before sending messages\n");
        udp_socket_write(sd, &client_addr, resp, strlen(resp));
        return NULL;
    }

    if (!payload || strlen(payload) == 0)
        return NULL; // silently ignore empty messages

    char out[BUFFER_SIZE];
    snprintf(out, BUFFER_SIZE, "%s: %s\n", sender->client.name, payload);

    broadcast_from_sender(sd, sender, out);
    return NULL;
    }


    // Handle sayto$user message
    if (strcmp(type, "sayto") == 0) {

        if (!sender) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$You must conn$<name> before sending messages\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }

        char *recipient_name = strtok(payload, " ");
        char *msg_rest = strtok(NULL, "");

        if (!recipient_name || !msg_rest) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$sayto requires a recipient and message\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }

        pthread_rwlock_rdlock(&clients_lock);
        ClientNode *recipient = find_client_by_name(recipient_name);
        pthread_rwlock_unlock(&clients_lock);

        if (!recipient) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$Recipient '%s' not found\n", recipient_name);
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }

        pthread_rwlock_rdlock(&clients_lock);
        int muted = recipient_has_muted_sender(recipient, sender->client.name);
        pthread_rwlock_unlock(&clients_lock);

        if (muted) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE,
                    "SYS$Your message could not be delivered (you are muted by %s)\n",
                    recipient->client.name);
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }

        char out[BUFFER_SIZE];
        snprintf(out, BUFFER_SIZE, "%s (private): %s\n",
                sender->client.name, msg_rest);

        udp_socket_write(sd, &recipient->client.addr, out, strlen(out));

        char ack[BUFFER_SIZE];
        snprintf(ack, BUFFER_SIZE, "SYS$Message delivered to %s\n", recipient->client.name);
        udp_socket_write(sd, &client_addr, ack, strlen(ack));

        return NULL;
    }


    // Handle mute$name 
    if (strcmp(type, "mute") == 0) {
        if (!sender) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$You must conn$<name> before muting users\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }
        if (payload == NULL || strlen(payload) == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$mute requires a client name\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }
        pthread_rwlock_wrlock(&clients_lock);
        sender = find_client_by_addr(&client_addr);
        int res = add_muted(sender, payload);
        pthread_rwlock_unlock(&clients_lock);
        if (res == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "SYS$You have muted %s\n", payload);
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
        } else {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$Unable to mute %s (maybe full list)\n", payload);
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
        }
        return NULL;
    }

    // Handle unmute$name 
    if (strcmp(type, "unmute") == 0) {
        if (!sender) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$You must conn$<name> before unmuting users\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }
        if (payload == NULL || strlen(payload) == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$unmute requires a client name\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }
        pthread_rwlock_wrlock(&clients_lock);
        sender = find_client_by_addr(&client_addr);
        int res = remove_muted(sender, payload);
        pthread_rwlock_unlock(&clients_lock);
        if (res == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "SYS$You have unmuted %s\n", payload);
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
        } else {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$%s was not muted\n", payload);
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
        }
        return NULL;
    }

    // Handle rename$new_name 
    if (strcmp(type, "rename") == 0) {
        if (!sender) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$You must conn$<name> before renaming\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }
        if (payload == NULL || strlen(payload) == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$rename requires a new name\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }
        pthread_rwlock_wrlock(&clients_lock);
        // check duplicate
        ClientNode *existing = find_client_by_name(payload);
        if (existing && existing != sender) {
            pthread_rwlock_unlock(&clients_lock);
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$Name '%s' already in use\n", payload);
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }
        char old_name[MAX_NAME_LEN];
        strncpy(old_name, sender->client.name, MAX_NAME_LEN);
        strncpy(sender->client.name, payload, MAX_NAME_LEN - 1);
        sender->client.name[MAX_NAME_LEN - 1] = '\0';
        pthread_rwlock_unlock(&clients_lock);

        char resp[BUFFER_SIZE];
        snprintf(resp, BUFFER_SIZE, "SYS$You are now known as %s\n", payload);
        udp_socket_write(sd, &client_addr, resp, strlen(resp));

        // broadcast rename to others
        char announce[BUFFER_SIZE];
        snprintf(announce, BUFFER_SIZE, "SYS$%s is now known as %s\n", old_name, payload);
        broadcast_all(sd, announce, sender);

        return NULL;
    }

    // Handle disconn$ (disconnect) 
    if (strcmp(type, "disconn") == 0) {
        if (!sender) {
            // might be not registered, but still reply
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "SYS$You are not connected\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }
        // remember name to broadcast
        char namebuf[MAX_NAME_LEN];
        pthread_rwlock_wrlock(&clients_lock);
        strncpy(namebuf, sender->client.name, MAX_NAME_LEN);
        remove_client(sender);
        pthread_rwlock_unlock(&clients_lock);

        char resp[BUFFER_SIZE];
        snprintf(resp, BUFFER_SIZE, "SYS$Disconnected. Bye!\n");
        udp_socket_write(sd, &client_addr, resp, strlen(resp));

        char announce[BUFFER_SIZE];
        snprintf(announce, BUFFER_SIZE, "SYS$%s has left the chat\n", namebuf);
        broadcast_all(sd, announce, NULL);
        return NULL;
    }

    // Handle kick$name (admin only: port 6666) 
    if (strcmp(type, "kick") == 0) {
        // check admin (requester port == 6666)
        int requester_port = ntohs(client_addr.sin_port);
        if (requester_port != 6666) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$kick is admin-only\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }
        if (payload == NULL || strlen(payload) == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$kick requires a client name\n");
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }
        pthread_rwlock_wrlock(&clients_lock);
        ClientNode *target = find_client_by_name(payload);
        if (!target) {
            pthread_rwlock_unlock(&clients_lock);
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$Client '%s' not found\n", payload);
            udp_socket_write(sd, &client_addr, resp, strlen(resp));
            return NULL;
        }

        char removed_name[MAX_NAME_LEN];
        strncpy(removed_name, target->client.name, MAX_NAME_LEN);

        struct sockaddr_in kicked_addr = target->client.addr;
        remove_client(target);

        pthread_rwlock_unlock(&clients_lock);

        // notify kicked client
        char notify_target[BUFFER_SIZE];
        snprintf(notify_target, BUFFER_SIZE, "SYS$You have been removed from the chat\n");
        udp_socket_write(sd, &kicked_addr, notify_target, strlen(notify_target));

        // broadcast
        char announce[BUFFER_SIZE];
        snprintf(announce, BUFFER_SIZE, "SYS$%s has been removed from the chat\n", removed_name);
        broadcast_all(sd, announce, NULL);

        return NULL;
    }

    //Unrecognised command 
    {
        char resp[BUFFER_SIZE];
        snprintf(resp, BUFFER_SIZE, "ERR$Unknown command '%s'\n", type);
        udp_socket_write(sd, &client_addr, resp, strlen(resp));
    }

    return NULL;
}
/*
 Background thread that removes inactive clients.
 Process:
   1. Periodically scan linked-list for least-recently-active client (LRU)
   2. If inactive > threshold → send ping
   3. If ping times out → remove client
*/
static void *monitor_thread(void *v) {
    int sd = *(int *)v;

    while (1) {
        sleep(MONITOR_INTERVAL);
        time_t now = time(NULL);

        ClientNode *target = NULL;
        time_t oldest = now;

        pthread_rwlock_rdlock(&clients_lock);
        ClientNode *cur = clients_head;

        while (cur) {
            if (cur->client.active && cur->client.last_active <= oldest) {
                oldest = cur->client.last_active;
                target = cur;
            }
            cur = cur->next;
        }

        pthread_rwlock_unlock(&clients_lock);

        if (!target)
            continue;   // no active clients at all

        if ((now - oldest) < INACTIVITY_THRESHOLD)
            continue;

        pthread_rwlock_wrlock(&clients_lock);

        // safety check – client might have been removed by worker thread
        ClientNode *check = clients_head;
        int still_exists = 0;
        while (check) {
            if (check == target) {
                still_exists = 1;
                break;
            }
            check = check->next;
        }

        if (!still_exists) {
            pthread_rwlock_unlock(&clients_lock);
            continue;
        }

        if (target->client.ping_sent == 0) {

            target->client.ping_sent = 1;
            target->client.ping_time = now;

            struct sockaddr_in addr = target->client.addr;
            pthread_rwlock_unlock(&clients_lock);

            char ping_msg[BUFFER_SIZE];
            snprintf(ping_msg, BUFFER_SIZE, "ping$");
            udp_socket_write(sd, &addr, ping_msg, strlen(ping_msg));

            continue;
        }

        time_t sent = target->client.ping_time;
        pthread_rwlock_unlock(&clients_lock);

        if ((now - sent) < PING_TIMEOUT)
            continue;

        pthread_rwlock_wrlock(&clients_lock);

        // client might have sent activity in the meantime
        if (!target->client.active || target->client.ping_sent == 0) {
            pthread_rwlock_unlock(&clients_lock);
            continue;
        }

        struct sockaddr_in kicked_addr = target->client.addr;
        char removed_name[MAX_NAME_LEN];
        strncpy(removed_name, target->client.name, MAX_NAME_LEN);
        removed_name[MAX_NAME_LEN - 1] = '\0';

        remove_client(target);
        pthread_rwlock_unlock(&clients_lock);

        char notify_target[BUFFER_SIZE];
        snprintf(notify_target, BUFFER_SIZE,
                 "SYS$You have been disconnected due to inactivity\n");
        udp_socket_write(sd, &kicked_addr, notify_target, strlen(notify_target));

        char announce[BUFFER_SIZE];
        snprintf(announce, BUFFER_SIZE,
                 "SYS$%s has been disconnected due to inactivity\n",
                 removed_name);
        broadcast_all(sd, announce, NULL);
    }

    return NULL;
}


// Main Server Loop
int main(int argc, char *argv[]) {
    // open UDP socket bound to SERVER_PORT
    int sd = udp_socket_open(SERVER_PORT);
    if (sd < 0) {
        fprintf(stderr, "Failed to open UDP socket on port %d\n", SERVER_PORT);
        return 1;
    }

    // Start monitor thread
    pthread_t monitor_tid;
    if (pthread_create(&monitor_tid, NULL, monitor_thread, &sd) != 0) {
        perror("pthread_create");
        close(sd);
        return 1;
    }
    pthread_detach(monitor_tid);

    while (1) {
        struct sockaddr_in client_addr;
        char client_request[BUFFER_SIZE];
        // read request (blocking)
        int rc = udp_socket_read(sd, &client_addr, client_request, BUFFER_SIZE);
        if (rc <= 0) {
            // read error or empty - ignore and continue
            continue;
        }
        client_request[rc < BUFFER_SIZE ? rc : BUFFER_SIZE-1] = '\0';

        // Spawn worker thread to handle request
        worker_arg_t *arg = malloc(sizeof(worker_arg_t));
        if (!arg) continue;
        arg->client_addr = client_addr;
        strncpy(arg->request, client_request, BUFFER_SIZE - 1);
        arg->request[BUFFER_SIZE - 1] = '\0';
        arg->sd = sd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, request_handler, arg) != 0) {
            perror("pthread_create");
            free(arg);
            continue;
        }
        pthread_detach(tid);
    }

    return 0;
}
