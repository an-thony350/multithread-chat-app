pkill chat_server
pkill chat_client

rm chat_server
rm chat_client

gcc chat_server.c -o chat_server 
gcc chat_client.c -o chat_client -lncurses -lpthread

