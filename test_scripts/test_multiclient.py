import socket
import time

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000

# Fn for printing section banners
def banner(text):
    print("\n" + "=" * 60)
    print(text)
    print("=" * 60)

# Create and return a UDP client socket
def make_client(label):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.settimeout(0.3)
    print(f"[{label}] Bound to {sock.getsockname()[1]}")
    return sock


# Send a message to the server
def send(sock, msg):
    sock.sendto(msg.encode(), (SERVER_IP, SERVER_PORT))

# Collect all packets that arrive within a short time window
def recv_all(sock, label):
    sock.setblocking(False)
    outputs = []
    while True:
        try:
            data, _ = sock.recvfrom(4096)
            text = data.rstrip(b"\x00").decode(errors="replace").strip()
            outputs.append(f"[{label}] {text}")
        except:
            break
    sock.setblocking(True)
    return outputs


# Clear any pending packets
def drain(sock):
    sock.setblocking(False)
    while True:
        try:
            sock.recvfrom(4096)
        except:
            break
    sock.setblocking(True)


# Print test result and return pass/fail
def expect(label, condition, ok, fail):
    if condition:
        print(f" [{label}] {ok}")
        return True
    else:
        print(f" [{label}] {fail}")
        return False



def main():
    all_ok = True

    banner("MULTI-CLIENT BEHAVIOUR TEST")

    # Create clients
    A = make_client("Alice")
    B = make_client("Bob")
    C = make_client("Charlie")

    banner("CONNECTING CLIENTS")

    send(A, "conn$ Alice")
    send(B, "conn$ Bob")
    send(C, "conn$ Charlie")
    time.sleep(1)

    drain(A)
    drain(B)
    drain(C)

    banner("BROADCAST TEST")

    send(A, "say$ Hello everyone!")
    time.sleep(0.5)

    bob = recv_all(B, "Bob")
    charlie = recv_all(C, "Charlie")

    all_ok &= expect("Broadcast Bob",
        any("Hello everyone!" in m for m in bob),
        "Bob received broadcast",
        "Bob did NOT receive broadcast"
    )

    all_ok &= expect("Broadcast Charlie",
        any("Hello everyone!" in m for m in charlie),
        "Charlie received broadcast",
        "Charlie did NOT receive broadcast"
    )

    banner("PRIVATE MESSAGE TEST (Bob → Charlie)")

    send(B, "sayto$ Charlie secret from Bob")
    time.sleep(0.5)

    alice = recv_all(A, "Alice")
    bob = recv_all(B, "Bob")
    charlie = recv_all(C, "Charlie")

    all_ok &= expect("Private visible to Charlie",
        any("secret from Bob" in m for m in charlie),
        "Charlie received private message",
        "Charlie did NOT receive private message"
    )

    all_ok &= expect("Private hidden from Alice",
        not any("secret from Bob" in m for m in alice),
        "Alice did not receive private message",
        "Alice incorrectly saw private message"
    )

    banner("MUTE TEST (Charlie mutes Alice)")

    send(C, "mute$ Alice")
    time.sleep(0.3)

    send(A, "say$ Charlie can't hear this")
    time.sleep(0.5)

    charlie = recv_all(C, "Charlie")

    all_ok &= expect("Mute",
        not any("can't hear" in m for m in charlie),
        "Muted message suppressed",
        "Muted message still arrived"
    )

    banner("UNMUTE TEST")

    send(C, "unmute$ Alice")
    time.sleep(0.3)

    send(A, "say$ Now Charlie CAN hear this")
    time.sleep(0.5)

    charlie = recv_all(C, "Charlie")

    all_ok &= expect("Unmute",
        any("CAN hear" in m for m in charlie),
        "Unmute worked",
        "Unmute failed"
    )

    banner("RENAME TEST")

    send(A, "rename$ Alice123")
    time.sleep(0.5)

    bob = recv_all(B, "Bob")

    all_ok &= expect("Rename propagation",
        any("Alice123" in m for m in bob),
        "Rename announced",
        "Rename not announced"
    )

    banner("DISCONNECT TEST")

    send(C, "disconn$")
    time.sleep(0.5)

    alice = recv_all(A, "Alice123")
    bob = recv_all(B, "Bob")

    all_ok &= expect("Disconnect broadcast",
        any("has left" in m or "disconnected" in m for m in bob),
        "Disconnect announced",
        "Disconnect not announced"
    )

    banner("FINAL RESULT")

    if all_ok:
        print(" ALL MULTI-CLIENT TESTS PASSED")
    else:
        print(" SOME MULTI-CLIENT TESTS FAILED")


if __name__ == "__main__":
    main()
