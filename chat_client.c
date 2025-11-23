#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <ncurses.h> // For UI
#include "udp.h"

#define CLIENT_PORT 55555
WINDOW *chat_pad;
WINDOW *input_win;

int chat_lines = 0;
int scroll_offset = 0;
volatile int should_exit = 0;

void redraw_pad();


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
        if (should_exit) return NULL;
        
        int rc = udp_socket_read(sd, &responder_addr, buffer, BUFFER_SIZE);
        if (rc > 0)
        {
            buffer[rc] = '\0';
            mvwprintw(chat_pad, chat_lines++, 0, "%s", buffer);

            int rows, cols;
            getmaxyx(stdscr, rows, cols);

            pnoutrefresh(
                chat_pad, 
                (chat_lines > rows - 2 ? chat_lines - (rows - 2) : 0), 
                0, 
                0, 0, 
                rows - 3, cols - 1
            );

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
        int ch;
        int input_pos = 0;
        client_request[0] = '\0';
        
        while ((ch = wgetch(input_win)))
        {
            if (ch == KEY_RESIZE || 
                ch == KEY_MOUSE ||
                ch == ERR) 
            {
                continue;
            }
            
            if (ch == KEY_DOWN) {
                if (scroll_offset > 0) 
                scroll_offset--;
                redraw_pad();
                continue;
                
            }
            if (ch == KEY_UP) {
                if (scroll_offset < chat_lines - 1) 
                scroll_offset++;
                redraw_pad();
                continue;
            }
            if (ch == '\n') break;

            if (ch == KEY_BACKSPACE || ch == 127) {
                if (input_pos > 0) {
                    input_pos--;
                    client_request[input_pos] = '\0';
                }
                wmove(input_win, 0, 2);
                wclrtoeol(input_win);
                wprintw(input_win, "%s", client_request);
                wrefresh(input_win);
                continue;
            }

            if (input_pos < BUFFER_SIZE - 1) {
                client_request[input_pos++] = ch;
                client_request[input_pos] = '\0';
                wclrtoeol(input_win);
                wrefresh(input_win);
            }
        }


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
            should_exit = 1;
            return NULL;
        }
    }

    return NULL;
}

void redraw_pad() {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int view_top = chat_lines - (rows - 2) - scroll_offset;
    if (view_top < 0) view_top = 0;

    pnoutrefresh(
        chat_pad,
        view_top, 0,
        0, 0,
        rows - 3, cols - 1
    );
    doupdate();
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
    mousemask(ALL_MOUSE_EVENTS, NULL);
    cbreak();
    noecho();
    curs_set(1);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    chat_pad = newpad(5000, cols);  // 5000 lines buffer (increase anytime)
    scrollok(chat_pad, TRUE);

    input_win = newwin(1, cols, rows - 1, 0);
    wprintw(input_win, "> ");
    wrefresh(input_win);

    mvhline(rows - 2, 0, '=', cols);
    wrefresh(stdscr);

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