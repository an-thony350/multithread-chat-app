#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "udp.h"

#define MAX_CLIENTS 128
#define MAX_NAME_LEN 64
#define MAX_MUTE 64

// Client record 
typedef struct {
    int active;                         
    char name[MAX_NAME_LEN];            
    struct sockaddr_in addr;             
    char muted[MAX_MUTE][MAX_NAME_LEN]; 
    int muted_count;
} Client;

// Global client table + lock protecting it
static Client clients[MAX_CLIENTS];
static pthread_rwlock_t clients_lock = PTHREAD_RWLOCK_INITIALIZER;

// Utility helpers 

//Compare two sockaddr_in for equality (IP + port) 
static int sockaddrs_equal(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return (a->sin_family == b->sin_family) &&
           (a->sin_port == b->sin_port) &&
           (a->sin_addr.s_addr == b->sin_addr.s_addr);
}

// Find client index by address (returns -1 if not found) 
static int find_client_by_addr(const struct sockaddr_in *addr) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active && sockaddrs_equal(&clients[i].addr, addr)) return i;
    }
    return -1;
}

// Find client index by name (returns -1 if not found) 
static int find_client_by_name(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active && strcmp(clients[i].name, name) == 0) return i;
    }
    return -1;
}

// Add client (assumes caller holds write lock). Returns index or -1 on failure 
static int add_client(const char *name, const struct sockaddr_in *addr) {
    // enforce unique name
    if (find_client_by_name(name) != -1) return -1;

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i].active) {
            clients[i].active = 1;
            strncpy(clients[i].name, name, MAX_NAME_LEN - 1);
            clients[i].name[MAX_NAME_LEN - 1] = '\0';
            clients[i].addr = *addr;
            clients[i].muted_count = 0;
            return i;
        }
    }
    return -1; 
}

// Remove client by index (assumes caller holds write lock) 
static void remove_client_by_index(int idx) {
    if (idx < 0 || idx >= MAX_CLIENTS) return;
    clients[idx].active = 0;
    clients[idx].name[0] = '\0';
    clients[idx].muted_count = 0;
    memset(&clients[idx].addr, 0, sizeof(clients[idx].addr));
}

// Add name to client's muted list (assumes write lock, idx valid) 
static int add_muted(int idx, const char *target) {
    if (idx < 0 || idx >= MAX_CLIENTS) return -1;
    if (clients[idx].muted_count >= MAX_MUTE) return -1;
    // avoid duplicate
    for (int i = 0; i < clients[idx].muted_count; ++i) {
        if (strcmp(clients[idx].muted[i], target) == 0) return 0;
    }
    strncpy(clients[idx].muted[clients[idx].muted_count], target, MAX_NAME_LEN - 1);
    clients[idx].muted[clients[idx].muted_count][MAX_NAME_LEN - 1] = '\0';
    clients[idx].muted_count++;
    return 0;
}

// Remove name from client's muted list (assumes write lock, idx valid)
static int remove_muted(int idx, const char *target) {
    if (idx < 0 || idx >= MAX_CLIENTS) return -1;
    int found = -1;
    for (int i = 0; i < clients[idx].muted_count; ++i) {
        if (strcmp(clients[idx].muted[i], target) == 0) { found = i; break; }
    }
    if (found == -1) return -1;
    // shift left
    for (int i = found; i < clients[idx].muted_count - 1; ++i) {
        strncpy(clients[idx].muted[i], clients[idx].muted[i+1], MAX_NAME_LEN);
    }
    clients[idx].muted_count--;
    clients[idx].muted[clients[idx].muted_count][0] = '\0';
    return 0;
}

// Check if recipient has muted sender (assumes caller has lock to read names safely) 
static int recipient_has_muted_sender(int recipient_idx, const char *sender_name) {
    if (recipient_idx < 0 || recipient_idx >= MAX_CLIENTS) return 0;
    for (int i = 0; i < clients[recipient_idx].muted_count; ++i) {
        if (strcmp(clients[recipient_idx].muted[i], sender_name) == 0) return 1;
    }
    return 0;
}

// Broadcast message to all clients (skip optional skip_idx, if >=0) 
// Uses udp_socket_write(sd, &client.addr, ...) to send 
static void broadcast_all(int sd, const char *msg, int skip_idx) {
    pthread_rwlock_rdlock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i].active) continue;
        if (i == skip_idx) continue;
        udp_socket_write(sd, &clients[i].addr, msg, BUFFER_SIZE);
    }
    pthread_rwlock_unlock(&clients_lock);
}

// Broadcast sender's message to all clients except those who muted the sender 
static void broadcast_from_sender(int sd, int sender_idx, const char *msg) {
    if (sender_idx < 0 || sender_idx >= MAX_CLIENTS) return;
    char sender_name[MAX_NAME_LEN];
    strncpy(sender_name, clients[sender_idx].name, MAX_NAME_LEN);
    sender_name[MAX_NAME_LEN - 1] = '\0';

    pthread_rwlock_rdlock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i].active) continue;
        if (i == sender_idx) continue;
        if (recipient_has_muted_sender(i, sender_name)) continue;
        udp_socket_write(sd, &clients[i].addr, msg, BUFFER_SIZE);
    }
    pthread_rwlock_unlock(&clients_lock);
}

// Worker thread argument 
typedef struct {
    struct sockaddr_in client_addr;
    char request[BUFFER_SIZE];
    int sd;
} worker_arg_t;

// Helper: trim end of line characters
static void rtrim(char *s) {
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) {
        s[len-1] = '\0';
        len--;
    }
}

// Trim leading and trailing spaces/tabs
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
        udp_socket_write(sd, &client_addr, err, BUFFER_SIZE);
        return NULL;
    }
    *d = '\0';
    char *type = buf;
    char *payload = d + 1;
    trim_spaces(type);
    trim_spaces(payload);


    // Identify sender index (if exists)
    pthread_rwlock_rdlock(&clients_lock);
    int sender_idx = find_client_by_addr(&client_addr);
    pthread_rwlock_unlock(&clients_lock);

    // Handle conn$name
    if (strcmp(type, "conn") == 0) {
        // payload = desired name
        if (payload == NULL || strlen(payload) == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$Name cannot be empty\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }

        // register under write lock
        pthread_rwlock_wrlock(&clients_lock);
        // check if already registered by address: update name if so
        int existing = find_client_by_addr(&client_addr);
        if (existing != -1) {
            // update name if not duplicate
            if (find_client_by_name(payload) != -1 && strcmp(clients[existing].name, payload) != 0) {
                pthread_rwlock_unlock(&clients_lock);
                char resp[BUFFER_SIZE];
                snprintf(resp, BUFFER_SIZE, "ERR$Name '%s' already in use\n", payload);
                udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
                return NULL;
            }
            strncpy(clients[existing].name, payload, MAX_NAME_LEN - 1);
            clients[existing].name[MAX_NAME_LEN - 1] = '\0';
            clients[existing].muted_count = clients[existing].muted_count; // keep mutes
            sender_idx = existing;
        } else {
            // new registration, ensure name not in use
            if (find_client_by_name(payload) != -1) {
                pthread_rwlock_unlock(&clients_lock);
                char resp[BUFFER_SIZE];
                snprintf(resp, BUFFER_SIZE, "ERR$Name '%s' already in use\n", payload);
                udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
                return NULL;
            }
            // add client
            int idx = add_client(payload, &client_addr);
            if (idx == -1) {
                pthread_rwlock_unlock(&clients_lock);
                char resp[BUFFER_SIZE];
                snprintf(resp, BUFFER_SIZE, "ERR$Server full or name taken\n");
                udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
                return NULL;
            }
            sender_idx = idx;
        }
        pthread_rwlock_unlock(&clients_lock);

        // Send confirmation to new client
        char welcome[BUFFER_SIZE];
        snprintf(welcome, BUFFER_SIZE, "SYS$Hi %s, you have successfully connected to the chat\n", payload);
        udp_socket_write(sd, &client_addr, welcome, BUFFER_SIZE);

        // Notify others
        char announce[BUFFER_SIZE];
        snprintf(announce, BUFFER_SIZE, "SYS$%s has joined the chat\n", payload);
        broadcast_all(sd, announce, sender_idx); // skip sender (sender already got welcome)

        return NULL;
    }

    // Handle say$message (broadcast) 
    if (strcmp(type, "say") == 0) {
        if (sender_idx == -1) {
            // sender not registered
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$You must conn$<name> before sending messages\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        // compose message: "Alice: Hello"
        char out[BUFFER_SIZE];
        snprintf(out, BUFFER_SIZE, "%s: %s\n", clients[sender_idx].name, payload);

        // broadcast to all, but respect mute lists
        broadcast_from_sender(sd, sender_idx, out);

        return NULL;
    }

    //Handle sayto$recipient message 
    if (strcmp(type, "sayto") == 0) {
        if (sender_idx == -1) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$You must conn$<name> before sending messages\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        // payload expected: "recipient message..."
        // extract recipient name (first token) and rest message
        char *recipient = strtok(payload, " ");
        char *msg_rest = strtok(NULL, ""); // the rest (may be NULL if empty message)
        if (!recipient || strlen(recipient) == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$sayto requires a recipient name and a message\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        int recv_idx;
        pthread_rwlock_rdlock(&clients_lock);
        recv_idx = find_client_by_name(recipient);
        pthread_rwlock_unlock(&clients_lock);
        if (recv_idx == -1) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$Recipient '%s' not found\n", recipient);
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        // check if recipient muted sender
        pthread_rwlock_rdlock(&clients_lock);
        int muted = recipient_has_muted_sender(recv_idx, clients[sender_idx].name);
        pthread_rwlock_unlock(&clients_lock);
        if (muted) {
            // act as if delivered silently (or optionally notify sender)
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "SYS$Your message could not be delivered (you are muted by %s)\n", recipient);
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        // send private message formatted: "Alice: hi\n"
        char out[BUFFER_SIZE];
        if (msg_rest)
            snprintf(out, BUFFER_SIZE, "%s (private): %s\n", clients[sender_idx].name, msg_rest);
        else
            snprintf(out, BUFFER_SIZE, "%s (private): \n", clients[sender_idx].name);
        udp_socket_write(sd, &clients[recv_idx].addr, out, BUFFER_SIZE);
        // optionally ack sender
        char ack[BUFFER_SIZE];
        snprintf(ack, BUFFER_SIZE, "SYS$Message delivered to %s\n", recipient);
        udp_socket_write(sd, &client_addr, ack, BUFFER_SIZE);

        return NULL;
    }

    // Handle mute$name 
    if (strcmp(type, "mute") == 0) {
        if (sender_idx == -1) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$You must conn$<name> before muting users\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        if (payload == NULL || strlen(payload) == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$mute requires a client name\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        pthread_rwlock_wrlock(&clients_lock);
        int res = add_muted(sender_idx, payload);
        pthread_rwlock_unlock(&clients_lock);
        if (res == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "SYS$You have muted %s\n", payload);
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
        } else {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$Unable to mute %s (maybe full list)\n", payload);
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
        }
        return NULL;
    }

    // Handle unmute$name 
    if (strcmp(type, "unmute") == 0) {
        if (sender_idx == -1) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$You must conn$<name> before unmuting users\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        if (payload == NULL || strlen(payload) == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$unmute requires a client name\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        pthread_rwlock_wrlock(&clients_lock);
        int res = remove_muted(sender_idx, payload);
        pthread_rwlock_unlock(&clients_lock);
        if (res == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "SYS$You have unmuted %s\n", payload);
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
        } else {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$%s was not muted\n", payload);
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
        }
        return NULL;
    }

    // Handle rename$new_name 
    if (strcmp(type, "rename") == 0) {
        if (sender_idx == -1) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$You must conn$<name> before renaming\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        if (payload == NULL || strlen(payload) == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$rename requires a new name\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        pthread_rwlock_wrlock(&clients_lock);
        // check duplicate
        if (find_client_by_name(payload) != -1) {
            pthread_rwlock_unlock(&clients_lock);
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$Name '%s' already in use\n", payload);
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        char old_name[MAX_NAME_LEN];
        strncpy(old_name, clients[sender_idx].name, MAX_NAME_LEN);
        strncpy(clients[sender_idx].name, payload, MAX_NAME_LEN - 1);
        clients[sender_idx].name[MAX_NAME_LEN - 1] = '\0';
        pthread_rwlock_unlock(&clients_lock);

        char resp[BUFFER_SIZE];
        snprintf(resp, BUFFER_SIZE, "SYS$You are now known as %s\n", payload);
        udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);

        // broadcast rename to others
        char announce[BUFFER_SIZE];
        snprintf(announce, BUFFER_SIZE, "SYS$%s is now known as %s\n", old_name, payload);
        broadcast_all(sd, announce, sender_idx);

        return NULL;
    }

    // Handle disconn$ (disconnect) 
    if (strcmp(type, "disconn") == 0) {
        if (sender_idx == -1) {
            // might be not registered, but still reply
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "SYS$You are not connected\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        // remember name to broadcast
        char namebuf[MAX_NAME_LEN];
        pthread_rwlock_wrlock(&clients_lock);
        strncpy(namebuf, clients[sender_idx].name, MAX_NAME_LEN);
        remove_client_by_index(sender_idx);
        pthread_rwlock_unlock(&clients_lock);

        char resp[BUFFER_SIZE];
        snprintf(resp, BUFFER_SIZE, "SYS$Disconnected. Bye!\n");
        udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);

        char announce[BUFFER_SIZE];
        snprintf(announce, BUFFER_SIZE, "SYS$%s has left the chat\n", namebuf);
        broadcast_all(sd, announce, -1);
        return NULL;
    }

    // Handle kick$name (admin only: port 6666) 
    if (strcmp(type, "kick") == 0) {
        // check admin (requester port == 6666)
        int requester_port = ntohs(client_addr.sin_port);
        if (requester_port != 6666) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$kick is admin-only\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        if (payload == NULL || strlen(payload) == 0) {
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$kick requires a client name\n");
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        pthread_rwlock_wrlock(&clients_lock);
        int target_idx = find_client_by_name(payload);
        if (target_idx == -1) {
            pthread_rwlock_unlock(&clients_lock);
            char resp[BUFFER_SIZE];
            snprintf(resp, BUFFER_SIZE, "ERR$Client '%s' not found\n", payload);
            udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
            return NULL;
        }
        // send removal message to target
        char notify_target[BUFFER_SIZE];
        snprintf(notify_target, BUFFER_SIZE, "SYS$You have been removed from the chat\n");
        udp_socket_write(sd, &clients[target_idx].addr, notify_target, BUFFER_SIZE);

        // remember name then remove
        char removed_name[MAX_NAME_LEN];
        strncpy(removed_name, clients[target_idx].name, MAX_NAME_LEN);
        remove_client_by_index(target_idx);
        pthread_rwlock_unlock(&clients_lock);

        // broadcast removal
        char announce[BUFFER_SIZE];
        snprintf(announce, BUFFER_SIZE, "SYS$%s has been removed from the chat\n", removed_name);
        broadcast_all(sd, announce, -1);

        return NULL;
    }

    //Unrecognised command 
    {
        char resp[BUFFER_SIZE];
        snprintf(resp, BUFFER_SIZE, "ERR$Unknown command '%s'\n", type);
        udp_socket_write(sd, &client_addr, resp, BUFFER_SIZE);
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

    // Zero client table
    memset(clients, 0, sizeof(clients));

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
