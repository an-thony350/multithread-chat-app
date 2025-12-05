/*
  chat_client.c
 
  Multithreaded UDP chat client with ncurses interface.
 
  Responsibilities:
   - Spawns two threads:
       1) Listener thread: receives and displays messages from server
       2) Sender thread: reads user input and sends commands/messages
   - Maintains a scrollable message buffer using an ncurses pad
 
  Thread safety:
   - All ncurses calls are protected by ui_lock
   - The listener thread updates chat history
   - The sender thread updates input field and scrolling state
 
  Shutdown:
   - On "disconn$", sender sets should_exit flag
   - Listener thread exits on next iteration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <time.h>
#include <ncurses.h> // For Terminal UI
#include "udp.h"

#define CLIENT_PORT 55555

//Global variables for ncurses
WINDOW *chat_pad;       // Pad for chat messages
WINDOW *input_win;      // Window for user input

int chat_lines = 0;
int scroll_offset = 0;
volatile int should_exit = 0;

// Mutex to protect ncurses UI updates
pthread_mutex_t ui_lock = PTHREAD_MUTEX_INITIALIZER;

void redraw_pad();

// Arguments passed into sender thread
struct sender_args {
    int sd;
    struct sockaddr_in addr;
};

/*
  listener_thread
 
  Listens for incoming UDP packets from the server and appends them
  to the chat window with timestamps.
 
  Responsibilities:
   - Receives message from server
   - Adds timestamps
   - Pushes text into ncurses pad
   - Keeps chat display scrolled to newest message unless user scrolls
 
  Thread safety:
   - UI updates must hold ui_lock
 */

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


            // Scroll display to newest message unless user has scrolled up
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

/*
  sender_thread
 
  Reads user keystrokes, builds command strings,
  and sends them to the server.
 
  Handles:
   - Editing input
   - Backspace behavior
   - Scrolling 
   - Message submission
 
  Terminates when "disconn$" is sent.
 */

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

/*
  redraw_pad
 
  Renders the visible portion of the chat pad based on scroll_offset.
  Called when:
   - New messages arrive
   - User presses up/down arrows
 
  Must hold ui_lock as it touches ncurses state.
 */

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

/*
  main
 
  Entry point for chat client.
 
  Responsibilities:
   - Set up UDP socket
   - Initialise ncurses UI
   - Launch sender + listener threads
   - Block until both terminate
   - Cleanup UI on exit
 */

int main(int argc, char *argv[])
{
    // Check if admin mode
    int is_admin = 0;
    if (argc > 1 && strcmp(argv[1], "--admin") == 0) {
        is_admin = 1;
    }
    
    int port_to_use = is_admin ? 6666 : CLIENT_PORT;
    
    // Open UDP socket & Configure server address
    int sd = udp_socket_open(port_to_use);
    struct sockaddr_in server_addr;
    assert(sd > -1);
    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);
    assert(rc == 0);

    pthread_t listener, sender;

    struct sender_args args;
    args.sd = sd;
    args.addr = server_addr;

    // Init terminal UI
    pthread_mutex_lock(&ui_lock);
    initscr();                          // Start ncurses mode
    cbreak();                           // Disable line buffering      
    noecho();                           // Prevent auto echo
    curs_set(1);                        // Show cursor 
    pthread_mutex_unlock(&ui_lock);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    chat_pad = newpad(5000, cols);  // 5k = buffer 
    scrollok(chat_pad, TRUE);

    // One line input window at bottom
    input_win = newwin(1, cols, rows - 1, 0);
    wprintw(input_win, "> ");
    wrefresh(input_win);

    // Divider line
    mvhline(rows - 2, 0, '=', cols);
    wrefresh(stdscr);

    // Enable arrow and special keys
    keypad(input_win, TRUE);
    wmove(input_win, 0, 2);  
    echo();                  

    // Launch threads
    pthread_create(&listener, NULL, listener_thread, &sd);

    pthread_create(&sender, NULL, sender_thread, &args);

    pthread_join(listener, NULL);
    pthread_join(sender, NULL);

    // shutdown UI
    endwin();
    return 0;
}