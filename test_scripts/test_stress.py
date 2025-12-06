#!/usr/bin/env python3
import socket
import random
import time
import threading
import string
import sys

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000

CLIENT_COUNT = 25
TEST_DURATION = 10
ACTIONS_PER_CLIENT = 200

PRINT_LOCK = threading.Lock()

# Thread-safe logging function
def log(msg):
    with PRINT_LOCK:
        print(msg)

# Generate a random username
def rand_name():
    return ''.join(random.choice(string.ascii_lowercase) for _ in range(6))

# Create and return a UDP client socket
def make_client():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.settimeout(0.15)
    return sock

# Send a message to the server
def send(sock, msg):
    sock.sendto(msg.encode(), (SERVER_IP, SERVER_PORT))

# Collect all packets that arrive within a short time window
def recv_all(sock):
    messages = []
    sock.setblocking(False)

    while True:
        try:
            data, _ = sock.recvfrom(4096)
            text = data.rstrip(b"\x00").decode(errors="replace").strip()
            messages.append(text)
        except:
            break

    sock.setblocking(True)
    return messages

# Client thread function
def client_thread(client_id):
    sock = make_client()
    name = f"User{client_id}"

    local_port = sock.getsockname()[1]
    log(f"[{name}] Started on port {local_port}")

    send(sock, f"conn$ {name}")
    time.sleep(0.1)
    recv_all(sock)   # ignore welcome + history

    start = time.time()
    actions_done = 0

    for _ in range(ACTIONS_PER_CLIENT):

        if time.time() - start > TEST_DURATION:
            break

        action = random.choice([
            "say", "sayto", "rename", "mute", "unmute"
        ])

        if action == "say":
            msg = f"say$ Hello from {name}"
            send(sock, msg)

        elif action == "sayto":
            target = f"User{random.randint(0, CLIENT_COUNT - 1)}"
            msg = f"sayto$ {target} private_{random.randint(0, 999)}"
            send(sock, msg)

        elif action == "rename":
            newname = rand_name()
            send(sock, f"rename$ {newname}")
            name = newname

        elif action == "mute":
            target = f"User{random.randint(0, CLIENT_COUNT - 1)}"
            send(sock, f"mute$ {target}")

        elif action == "unmute":
            target = f"User{random.randint(0, CLIENT_COUNT - 1)}"
            send(sock, f"unmute$ {target}")

        responses = recv_all(sock)
        for r in responses:
            if "ERR$" in r:
                log(f"[{name}] ERROR: {r}")

        actions_done += 1

        # random pacing avoids accidental synchronization
        time.sleep(random.uniform(0.005, 0.04))

    send(sock, "disconn$")
    time.sleep(0.1)
    recv_all(sock)

    sock.close()
    log(f"[{name}] Finished after {actions_done} actions")


# Main test function
def main():
    print("\n" + "=" * 70)
    print("CONCURRENCY STRESS TEST")
    print("Clients:", CLIENT_COUNT)
    print("Duration:", TEST_DURATION, "seconds")
    print("Max actions per client:", ACTIONS_PER_CLIENT)
    print("=" * 70 + "\n")

    threads = []
    start = time.time()
    
    for i in range(CLIENT_COUNT):
        th = threading.Thread(target=client_thread, args=(i,))
        th.start()
        threads.append(th)
        time.sleep(0.03)   

    for th in threads:
        th.join()

    elapsed = time.time() - start

    print("\n" + "=" * 70)
    print("STRESS TEST COMPLETE")
    print("=" * 70)
    print(f"Elapsed time: {elapsed:.2f} seconds\n")

    print("What to check on server:")
    print(" - No crashes / segfaults")
    print(" - No deadlocks")
    print(" - No runaway memory usage")
    print(" - No corrupted usernames")
    print(" - No missing / out-of-order state changes")
    print(" - No infinite ping loops")
    print(" - Graceful handling of duplicate names\n")


if __name__ == "__main__":
    main()
