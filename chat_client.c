#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <ncurses.h> // For UI
#include "udp.h"

#define CLIENT_PORT 55555
WINDOW *chat_win;
WINDOW *input_win;

    struct sender_args {
        int sd;
        struct sockaddr_in addr;
    };

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
            wprintw(chat_win, "%s\n", buffer);
            wrefresh(chat_win);

            wrefresh(input_win);
        }
    }
    return NULL;
}

// Sender Thread Function
void *sender_thread(void *arg)
{

    struct sender_args *args = arg;

    int sd = args->sd;
    struct sockaddr_in server_addr = args->addr;

    char client_request[BUFFER_SIZE];

    while (1)
    {
        // Read input from user
        wgetnstr(input_win, client_request, BUFFER_SIZE);


        client_request[strcspn(client_request, "\n")] = '\0';

        if (strlen(client_request) == 0)
            continue;

        // Send the raw request to the server
        udp_socket_write(sd, &server_addr, client_request, strlen(client_request));

        wclear(input_win);
        mvwprintw(input_win, 0, 0, "> ");
        wrefresh(input_win);

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
    struct sockaddr_in server_addr;
    
    assert(sd > -1);

    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);
    assert(rc == 0);

    pthread_t listener, sender;

    struct sender_args args;
    args.sd = sd;
    args.addr = server_addr;

    initscr();
    cbreak();
    noecho();
    curs_set(1);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    chat_win = newwin(rows - 1, cols, 0, 0);
    scrollok(chat_win, TRUE);
    input_win = newwin(1, cols, rows - 1, 0);
    wprintw(input_win, "> ");
    wrefresh(input_win);

    keypad(input_win, TRUE);
    wmove(input_win, 0, 2);  
    echo();                  


    pthread_create(&listener, NULL, listener_thread, &sd);

    pthread_create(&sender, NULL, sender_thread, &args);

    pthread_join(listener, NULL);
    pthread_join(sender, NULL);

    endwin();
    return 0;
}