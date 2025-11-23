pkill chat_server
pkill chat_client
pkill admin_client

rm chat_server
rm chat_client
rm admin_client

gcc chat_server.c -o chat_server 
gcc chat_client.c -o chat_client -lncurses -lpthread
gcc admin_client.c -o admin_client -lncurses -lpthread
