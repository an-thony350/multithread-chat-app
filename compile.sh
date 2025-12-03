# Kill any running instances of the chat server and client & Remove old binaries
pkill chat_server
pkill chat_client

rm chat_server
rm chat_client

# Compile the chat server and client
gcc chat_server.c -o chat_server 
gcc chat_client.c -o chat_client -lncurses -lpthread
