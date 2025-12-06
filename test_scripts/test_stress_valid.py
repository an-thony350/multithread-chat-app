import socket
import random
import time
import threading

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000

CLIENT_COUNT = 25
TEST_DURATION = 10

ACTIVE_USERS = {}
LOCK = threading.Lock()

# Create and return a UDP client socket
def make_client():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.settimeout(0.2)
    return sock

# Send a message to the server
def send(sock, msg):
    sock.sendto(msg.encode(), (SERVER_IP, SERVER_PORT))

# Clear any pending packets
def drain(sock):
    sock.setblocking(False)
    while True:
        try:
            sock.recvfrom(4096)
        except:
            break
    sock.setblocking(True)

# Get a list of active users, excluding the given name
def safe_users(exclude=None):
    with LOCK:
        users = list(ACTIVE_USERS.values())
    if exclude and exclude in users:
        users.remove(exclude)
    return users

# Client thread function
def client_thread(i):
    sock = make_client()
    name = f"User{i}"

    with LOCK:
        ACTIVE_USERS[i] = name

    send(sock, f"conn${name}")
    drain(sock)
    time.sleep(0.1)

    start = time.time()

    while time.time() - start < TEST_DURATION:

        with LOCK:
            my_name = ACTIVE_USERS[i]

        action = random.choice(["say", "sayto", "rename", "mute", "unmute"])
        targets = safe_users(exclude=my_name)

        # broadcast
        if action == "say":
            send(sock, f"say$Hello from {my_name}")

        # private
        elif action == "sayto" and targets:
            target = random.choice(targets)
            send(sock, f"sayto${target} hi")

        # rename
        elif action == "rename":
            newname = f"{my_name}_{random.randint(0,999)}"
            send(sock, f"rename${newname}")
            with LOCK:
                ACTIVE_USERS[i] = newname

        # mute
        elif action == "mute" and targets:
            target = random.choice(targets)
            send(sock, f"mute${target}")

        # unmute
        elif action == "unmute" and targets:
            target = random.choice(targets)
            send(sock, f"unmute${target}")

        time.sleep(random.uniform(0.01, 0.04))
        drain(sock)

    send(sock, "disconn$")
    drain(sock)
    sock.close()
    print(f"[{i}] finished cleanly")


# Main test function
def main():
    print("\n" + "="*60)
    print("VALID LOAD STRESS TEST")
    print("="*60)

    threads = []

    for i in range(CLIENT_COUNT):
        t = threading.Thread(target=client_thread, args=(i,))
        t.start()
        threads.append(t)
        time.sleep(0.02)

    for t in threads:
        t.join()

    print("\nTEST COMPLETE")
    print("Server should show:")
    print(" - No ERR$ spam")
    print(" - Clean joins/leaves")
    print(" - Valid PMs only")
    print(" - No crashes or deadlocks")

if __name__ == "__main__":
    main()
