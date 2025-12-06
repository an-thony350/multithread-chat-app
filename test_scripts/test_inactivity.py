import socket
import time

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000

MAX_WAIT_TIME = 45   # must exceed inactivity + ping timeout + monitor delay

# Fn for printing section banners
def banner(text):
    print("\n" + "=" * 60)
    print(text)
    print("=" * 60)

# Create and return a UDP client socket
def make_client(label):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.settimeout(0.5)
    return sock

# Send a message to the server
def send(sock, msg):
    sock.sendto(msg.encode(), (SERVER_IP, SERVER_PORT))

# Collect all packets that arrive within a short time window
def recv_all(sock, label):
    results = []
    sock.setblocking(False)
    while True:
        try:
            data, _ = sock.recvfrom(4096)
            text = data.rstrip(b"\x00").decode(errors="replace").strip()
            results.append(f"[{label}] {text}")
        except:
            break
    sock.setblocking(True)
    return results

def main():

    banner("PE2 INACTIVITY MONITOR TEST")

    # Create two clients: one active, one idle
    Active = make_client("Active")
    Idle   = make_client("Idle")

    banner("CONNECTING CLIENTS")

    send(Active, "conn$ActiveClient")
    send(Idle,   "conn$IdleClient")
    time.sleep(1)

    for out in recv_all(Active, "Active"): print(out)
    for out in recv_all(Idle,   "Idle"):   print(out)

    banner("ACTIVE CLIENT WILL STAY ACTIVE")

    # Active client sends periodic messages to stay active
    for i in range(3):
        print(f"\n[Active] sending keepalive message {i + 1}")
        send(Active, "say$ Still here")
        time.sleep(1)

        for out in recv_all(Active, "Active"): print(out)
        for out in recv_all(Idle,   "Idle"):   print(out)

    banner("IDLE CLIENT NOW GOES QUIET")
    print("Waiting for ping + removal...")

    # State flags
    idle_pinged = False
    idle_kicked = False
    removal_seen = False

    start = time.time()

    # Monitor both clients for server messages
    while time.time() - start < MAX_WAIT_TIME:

        for msg in recv_all(Idle, "Idle"):
            print(msg)

            if "ping$" in msg:
                idle_pinged = True

            if "disconnected" in msg.lower():
                idle_kicked = True

        for msg in recv_all(Active, "Active"):
            print(msg)

            if "disconnected" in msg.lower() or "removed" in msg.lower():
                removal_seen = True

        # Check if all conditions met
        if idle_pinged and idle_kicked and removal_seen:
            break

        time.sleep(0.5)

    banner("RESULT ANALYSIS")

    print("Idle client received ping:      ", idle_pinged)
    print("Idle client disconnected:       ", idle_kicked)
    print("Active client saw removal:      ", removal_seen)

    # Final result
    if idle_pinged and idle_kicked and removal_seen:
        print("\n PE2 RESULT: PASS — Inactivity logic works correctly.")
    else:
        print("\n PE2 RESULT: FAIL — behaviour incomplete.\n")
        print("Status breakdown:")

        if not idle_pinged:
            print(" - ping$ never observed")
        if not idle_kicked:
            print(" - Idle client was not removed")
        if not removal_seen:
            print(" - Active client did not observe removal broadcast")
    
        print("\nNOTE: With MONITOR_INTERVAL=10 and PING_TIMEOUT=20,")
        print("server removal can take up to ~40 seconds.")

    send(Active, "disconn$")
    time.sleep(0.5)


if __name__ == "__main__":
    main()
