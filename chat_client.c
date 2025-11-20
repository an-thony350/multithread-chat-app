#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "udp.h"


// Listener Thread Function
void *listener_thread(void *arg)
{
    int sd = *(int *)arg;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in responder_addr;

    while (1)
    {
        int rc = udp_socket_read(sd, &responder_addr, buffer, BUFFER_SIZE);
        if (rc > 0)
        {
            buffer[rc] = '\0';
            printf("%s\n", buffer);
            fflush(stdout);
        }
    }
    return NULL;
}

// Sender Thread Function
void *sender_thread(void *arg)
{
    struct sender_args {
        int sd;
        struct sockaddr_in addr;
    };

    struct sender_args *args = arg;

    int sd = args->sd;
    struct sockaddr_in server_addr = args->addr;

    char client_request[BUFFER_SIZE];

    while (1)
    {
        // Read input from user
        if (fgets(client_request, BUFFER_SIZE, stdin) == NULL)
            continue;

        client_request[strcspn(client_request, "\n")] = '\0';

        if (strlen(client_request) == 0)
            continue;

        // Send the raw request to the server
        udp_socket_write(sd, &server_addr, client_request, BUFFER_SIZE);

        // If the user typed disconn$, quit the client
        if (strncmp(client_request, "disconn$", 8) == 0)
        {
            exit(0);
        }
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    int sd = udp_socket_open(CLIENT_PORT);
    assert(sd > -1);

    struct sockaddr_in server_addr, responder_addr;

    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);

    // Storage for request and response messages
    char client_request[BUFFER_SIZE], server_response[BUFFER_SIZE];

    // Demo code (remove later)
    strcpy(client_request, "Dummy Request");

    // This function writes to the server (sends request)
    // through the socket at sd.
    // (See details of the function in udp.h)
    rc = udp_socket_write(sd, &server_addr, client_request, BUFFER_SIZE);

    if (rc > 0)
    {
        // This function reads the response from the server
        // through the socket at sd.
        // In our case, responder_addr will simply be
        // the same as server_addr.
        // (See details of the function in udp.h)
        int rc = udp_socket_read(sd, &responder_addr, server_response, BUFFER_SIZE);

        // Demo code (remove later)
        printf("server_response: %s", server_response);
    }

    return 0;
}