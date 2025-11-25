import socket
import random
import time
import threading
import string

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000

CLIENT_COUNT = 25         
TEST_DURATION = 10         
ACTIONS_PER_CLIENT = 200   

# ------------------------------------------------------------
# Helper Functions
# ------------------------------------------------------------

def rand_name():
    return ''.join(random.choice(string.ascii_lowercase) for _ in range(6))

def make_client():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.settimeout(0.15)
    return sock

def send(sock, msg):
    sock.sendto(msg.encode(), (SERVER_IP, SERVER_PORT))

def drain(sock):
    """Read and ignore all incoming server packets."""
    sock.setblocking(False)
    while True:
        try: sock.recvfrom(4096)
        except: break
    sock.setblocking(True)

# ------------------------------------------------------------
# Client Thread Function
# ------------------------------------------------------------

def client_thread(id_num):
    sock = make_client()
    name = f"User{id_num}"

    # connect
    send(sock, f"conn${name}")
    drain(sock)
    time.sleep(0.05)

    start = time.time()

    for _ in range(ACTIONS_PER_CLIENT):

        # stop early if time is up
        if time.time() - start > TEST_DURATION:
            break

        action = random.choice(["say", "sayto", "rename", "mute", "unmute"])

        if action == "say":
            msg = f"say$Hello_from_{name}"
            send(sock, msg)

        elif action == "sayto":
            target = f"User{random.randint(0, CLIENT_COUNT-1)}"
            msg = f"sayto${target} secret_{random.randint(0,100)}"
            send(sock, msg)

        elif action == "rename":
            newname = rand_name()
            name = newname
            send(sock, f"rename${newname}")

        elif action == "mute":
            target = f"User{random.randint(0, CLIENT_COUNT-1)}"
            send(sock, f"mute${target}")

        elif action == "unmute":
            target = f"User{random.randint(0, CLIENT_COUNT-1)}"
            send(sock, f"unmute${target}")

        # small random spacing prevents purely synchronous bursts
        time.sleep(random.uniform(0.005, 0.03))

        # drain any replies, we don’t care what they are
        drain(sock)

    # disconnect
    send(sock, "disconn$")
    drain(sock)
    sock.close()

# ------------------------------------------------------------
# MAIN TEST DRIVER
# ------------------------------------------------------------

def main():
    print("\n" + "="*60)
    print("CONCURRENCY STRESS TEST: STARTING")
    print("="*60)

    threads = []

    # spin up client threads
    for i in range(CLIENT_COUNT):
        th = threading.Thread(target=client_thread, args=(i,))
        th.start()
        threads.append(th)
        time.sleep(0.02)  # stagger start a bit

    # wait for all to finish
    for th in threads:
        th.join()

    print("\n" + "="*60)
    print("CONCURRENCY STRESS TEST: COMPLETE")
    print("="*60)
    print("\nCheck server output for:")
    print(" - No crashes / segfaults")
    print(" - No deadlocks")
    print(" - Consistent formatting")
    print(" - Valid system messages")
    print(" - No corruption in user list operations\n")

if __name__ == "__main__":
    main()
