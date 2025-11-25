import socket
import time

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000

# FOR THIS CODE, CHANGE THE SERVER'S INACTIVITY TIMEOUT TO 10 SECONDS (IN CHAT_SERVER.C) FOR TESTING

def make_client(label):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.settimeout(0.5)
    return sock

def send(sock, msg):
    sock.sendto(msg.encode(), (SERVER_IP, SERVER_PORT))

def recv_all(sock, label):
    """Return all packets tagged with client label."""
    outputs = []
    sock.setblocking(False)
    while True:
        try:
            data, _ = sock.recvfrom(4096)
            text = data.rstrip(b"\x00").decode(errors="replace").strip()
            outputs.append(f"[{label}] {text}")
        except:
            break
    sock.setblocking(True)
    return outputs

def banner(text):
    print("\n" + "="*60)
    print(text)
    print("="*60)

def main():

    banner("PE2 INACTIVE CLIENT TEST")

    # Create two clients
    Active = make_client("Active")
    Idle = make_client("Idle")

    # CONNECT BOTH
    banner("Connecting clients...")
    send(Active, "conn$ActiveClient")
    send(Idle,   "conn$IdleClient")
    time.sleep(1)

    for out in recv_all(Active, "Active"): print(out)
    for out in recv_all(Idle,   "Idle"):   print(out)

    # ----------------------------------------------------------
    banner("Letting IdleClient go inactive...")
    # ----------------------------------------------------------

    # Active stays active
    for i in range(3):
        time.sleep(3)     # idle for 3 seconds at a time
        send(Active, "say$Still here!")
        time.sleep(0.5)

        # Read just for logging
        for out in recv_all(Active, "Active"): print(out)
        for out in recv_all(Idle,   "Idle"):   print(out)

    # Now we wait silently until the server hits inactivity threshold
    banner("Waiting silently for server to ping IdleClient...")

    # Let server detect inactivity (threshold must be short!)
    time.sleep(15)

    # Fetch any pings and removal messages
    idle_msgs   = recv_all(Idle, "Idle")
    active_msgs = recv_all(Active, "Active")

    banner("Messages received:")
    for msg in idle_msgs:   print(msg)
    for msg in active_msgs: print(msg)

    # ANALYSIS
    banner("RESULTS")

    idle_got_ping = any("ping$" in msg for msg in idle_msgs)
    active_saw_removal = any("has been removed" in msg or "disconnected" in msg for msg in active_msgs)

    print("Idle client received ping:      ", idle_got_ping)
    print("Active client saw removal:      ", active_saw_removal)

    if idle_got_ping and active_saw_removal:
        print("\nPE2 RESULT: PASS — Inactive client detected and removed correctly.")
    else:
        print("\nPE2 RESULT: FAIL — Behaviour not fully correct.")

    # CLEANUP
    send(Active, "disconn$")
    time.sleep(0.2)

if __name__ == "__main__":
    main()
