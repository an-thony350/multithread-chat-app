#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <time.h>
#include <ncurses.h> // Terminal UI
#include "udp.h"

#define CLIENT_PORT 55555

WINDOW *chat_pad;
WINDOW *input_win;

int chat_lines = 0;
int scroll_offset = 0;
volatile int should_exit = 0;
pthread_mutex_t ui_lock = PTHREAD_MUTEX_INITIALIZER;

void redraw_pad();

// Structure to pass arguments to sender thread
struct sender_args {
    int sd;
    struct sockaddr_in addr;
};

// Thread that listens to incoming messages from server
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
            if (rc >= BUFFER_SIZE) rc = BUFFER_SIZE - 1;
            buffer[rc] = '\0';
            
            // Generate timestamp 
            time_t t = time(NULL);
            struct tm *tm_info = localtime(&t);
            char timestamp[16];
            strftime(timestamp, sizeof(timestamp), "[%H:%M]", tm_info);

            int line = chat_lines;
            char *msg_text = buffer;

            if (strncmp(buffer, "[History]", 10) == 0) {
                msg_text = buffer + 10; // skip "[History]"
            }

            pthread_mutex_lock(&ui_lock);

            mvwprintw(chat_pad, chat_lines++, 0, "%s", msg_text);
    
            // Print timestamp flush right
            int rows, cols;
            getmaxyx(stdscr, rows, cols);
            
            int ts_col = cols - (int)strlen(timestamp) - 1;
            if (ts_col < 0) ts_col = 0;
            mvwprintw(chat_pad, chat_lines - 1, ts_col, "%s", timestamp);


            // Refresh visible portion of chat pad
            // If message exceeds visible area, show the bottom part
            pnoutrefresh(
                chat_pad, 
                (chat_lines > rows - 2 ? chat_lines - (rows - 2) : 0), 
                0, 
                0, 0, 
                rows - 3, cols - 1
            );
            // Refresh input window to prevent overlap
            wrefresh(input_win);

            pthread_mutex_unlock(&ui_lock);
        }
    }
    return NULL;
}

// Thread that handles user input and sends messages
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
            // Handles special keys
            if (ch == KEY_RESIZE || ch == ERR) 
            {
                continue;
            }
            // Scroll chat pad
            if (ch == KEY_DOWN) {
                if (scroll_offset > 0) 
                scroll_offset--;
                redraw_pad();
                continue;
                
            }
            if (ch == KEY_UP) {
                int rows, cols;
                getmaxyx(stdscr, rows, cols);
                int max_scroll = chat_lines - (rows - 2);
            if (max_scroll < 0) max_scroll = 0;

            if (scroll_offset < max_scroll)
                scroll_offset++;
                redraw_pad();
                continue;
            }
            if (ch == '\n') break;
            // Handles backspace
            if (ch == KEY_BACKSPACE || ch == 127) {
                if (input_pos > 0) {
                    input_pos--;
                    client_request[input_pos] = '\0';
                }
                pthread_mutex_lock(&ui_lock);
                wmove(input_win, 0, 2);
                wclrtoeol(input_win);
                wprintw(input_win, "%s", client_request);
                wrefresh(input_win);
                pthread_mutex_unlock(&ui_lock);
                continue;
            }
            // Append normal characters
            if (input_pos < BUFFER_SIZE - 1) {
                client_request[input_pos++] = ch;
                client_request[input_pos] = '\0';
                pthread_mutex_lock(&ui_lock);
                wclrtoeol(input_win);
                wrefresh(input_win);
                pthread_mutex_unlock(&ui_lock);
            }
        }


        client_request[strcspn(client_request, "\n")] = '\0';

        if (strlen(client_request) == 0)
            continue;

        // Send the raw request to the server
        udp_socket_write(sd, &server_addr, client_request, strlen(client_request));

        pthread_mutex_lock(&ui_lock);
        wclear(input_win);
        mvwprintw(input_win, 0, 0, "> ");
        wrefresh(input_win);
        pthread_mutex_unlock(&ui_lock);

        // Quit client on request
        if (strncmp(client_request, "disconn$", 8) == 0)
        {
            should_exit = 1;
            return NULL;
        }
    }

    return NULL;
}

// Refresh visible portion of chat pad
void redraw_pad() {
    pthread_mutex_lock(&ui_lock);
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
    pthread_mutex_unlock(&ui_lock);
}


int main(int argc, char *argv[])
{
    // Check if admin mode
    int is_admin = 0;

    if (argc > 1 && strcmp(argv[1], "--admin") == 0) {
        is_admin = 1;
    }
    
    int port_to_use = is_admin ? 6666 : CLIENT_PORT;
    int sd = udp_socket_open(port_to_use);

    struct sockaddr_in server_addr;
    
    assert(sd > -1);

    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);
    assert(rc == 0);

    pthread_t listener, sender;

    struct sender_args args;
    args.sd = sd;
    args.addr = server_addr;

    pthread_mutex_lock(&ui_lock);
    // Init terminal UI
    initscr();
    cbreak();
    noecho();
    curs_set(1);
    pthread_mutex_unlock(&ui_lock);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    chat_pad = newpad(5000, cols);  // 5000 lines buffer 
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