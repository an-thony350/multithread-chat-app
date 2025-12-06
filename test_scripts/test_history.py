import socket
import time

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000

# Create and return a UDP client socket
def make_client():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.settimeout(0.1)
    return sock

# Send a message to the server
def send(sock, msg):
    sock.sendto(msg.encode(), (SERVER_IP, SERVER_PORT))

# Collect all packets that arrive within a short time window
def recv_all(sock, max_wait=1.0):
    results = []
    end = time.time() + max_wait
    while time.time() < end:
        try:
            data, _ = sock.recvfrom(4096)
            text = data.rstrip(b"\x00").decode(errors="replace").strip()
            results.append(text)
        except socket.timeout:
            pass
    return results

def main():
    print("\n" + "="*60)
    print("PE1 HISTORY BUFFER TEST")
    print("="*60)
    
    # First client connects and generates chat history
    A = make_client()
    send(A, "conn$ HistoryMaker")
    time.sleep(0.5)
    recv_all(A)

    print("\nGenerating 20 broadcast messages...\n")
    
    # Send 20 broadcast messages to populate history buffer
    for i in range(20):
        send(A, f"say$ Message_{i}")
        time.sleep(0.05)
        recv_all(A)

    print("Finished sending 20 messages.")
    print("Connecting second client to receive history...\n")

    # Second client connects and should receive last N history entries
    B = make_client()
    send(B, "conn$ HistoryTester")
    history_packets = recv_all(B, max_wait=1.5)
    
    # Extract history-only packets
    history_only = [h for h in history_packets if h.startswith("[History]")]

    print("\nHistory received:")
    for h in history_only:
        print(" ", h)

    # Validate count
    if len(history_only) != 15:
        print("\nFAIL: Expected 15 history entries, received", len(history_only))
        return

    expected = [f"[History] HistoryMaker: Message_{i}" for i in range(5, 20)]

    mismatched = False
    for e, g in zip(expected, history_only):
        if e != g:
            print("\nMismatch detected:")
            print("Expected:", e)
            print("Got:     ", g)
            mismatched = True

    if not mismatched:
        print("\nRESULT: PASS — History buffer is correct.")
    else:
        print("\nRESULT: FAIL — History output incorrect.")

    send(A, "disconn$")
    send(B, "disconn$")

if __name__ == "__main__":
    main()
