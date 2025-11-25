import socket
import time
import threading

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000

def create_client():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.settimeout(0.3)
    return sock

def send(sock, msg):
    sock.sendto(msg.encode(), (SERVER_IP, SERVER_PORT))

def recv_all(sock, label):
    """Receive all available packets for a client and print them."""
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

# ------------------------------------------------------------
# TEST SEQUENCE
# ------------------------------------------------------------

def main():
    print("\n" + "="*60)
    print("MULTI-CLIENT BEHAVIOUR TEST")
    print("="*60)

    # Create 3 simulated clients
    A = create_client()
    B = create_client()
    C = create_client()

    # --------------------------------------------------------
    print("\nCONNECTING CLIENTS")
    # --------------------------------------------------------
    send(A, "conn$Alice")
    send(B, "conn$Bob")
    send(C, "conn$Charlie")
    time.sleep(0.5)

    for out in recv_all(A, "Alice"): print(out)
    for out in recv_all(B, "Bob"): print(out)
    for out in recv_all(C, "Charlie"): print(out)

    # --------------------------------------------------------
    print("\nBROADCAST TEST")
    # --------------------------------------------------------
    send(A, "say$Hello everyone!")
    time.sleep(0.5)

    for out in recv_all(A, "Alice"): print(out)
    for out in recv_all(B, "Bob"): print(out)
    for out in recv_all(C, "Charlie"): print(out)

    # --------------------------------------------------------
    print("\nPRIVATE MESSAGE TEST (Bob → Charlie)")
    # --------------------------------------------------------
    send(B, "sayto$Charlie secret from Bob")
    time.sleep(0.5)

    for out in recv_all(A, "Alice"): print(out)    # should NOT see PM
    for out in recv_all(B, "Bob"): print(out)
    for out in recv_all(C, "Charlie"): print(out)  # should see PM

    # --------------------------------------------------------
    print("\nMUTE TEST (Charlie mutes Alice)")
    # --------------------------------------------------------
    send(C, "mute$Alice")
    time.sleep(0.3)

    send(A, "say$Charlie can't hear this")
    time.sleep(0.5)

    for out in recv_all(C, "Charlie"): print(out)  # should NOT receive from Alice

    # --------------------------------------------------------
    print("\nUNMUTE TEST (Charlie unmutes Alice)")
    # --------------------------------------------------------
    send(C, "unmute$Alice")
    time.sleep(0.3)

    send(A, "say$Now Charlie CAN hear this")
    time.sleep(0.5)

    for out in recv_all(C, "Charlie"): print(out)  # should now receive

    # --------------------------------------------------------
    print("\nRENAME TEST (Alice → Alice123)")
    # --------------------------------------------------------
    send(A, "rename$Alice123")
    time.sleep(0.5)

    for out in recv_all(B, "Bob"): print(out)
    for out in recv_all(C, "Charlie"): print(out)

    # --------------------------------------------------------
    print("\nDISCONNECT TEST (Charlie disconnects)")
    # --------------------------------------------------------
    send(C, "disconn$")
    time.sleep(0.5)

    for out in recv_all(A, "Alice123"): print(out)
    for out in recv_all(B, "Bob"): print(out)
    for out in recv_all(C, "Charlie"): print(out)

    print("\nTEST COMPLETE\n")


if __name__ == "__main__":
    main()
