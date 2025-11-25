import socket
import time

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000

def make_client():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.settimeout(0.4)
    return sock

def send(sock, msg):
    sock.sendto(msg.encode(), (SERVER_IP, SERVER_PORT))

def recv_all(sock):
    """Return all packets available right now."""
    results = []
    sock.setblocking(False)
    while True:
        try:
            data, _ = sock.recvfrom(4096)
            text = data.rstrip(b"\x00").decode(errors="replace").strip()
            results.append(text)
        except:
            break
    sock.setblocking(True)
    return results

def main():
    print("\n" + "="*60)
    print("PE1 HISTORY BUFFER TEST")
    print("="*60)

    # Client A will generate history
    A = make_client()
    send(A, "conn$HistoryMaker")
    time.sleep(0.4)
    recv_all(A)

    print("\nGenerating 20 broadcast messages...\n")

    # Send 20 broadcast messages
    for i in range(20):
        send(A, f"say$Message_{i}")
        time.sleep(0.1)
        recv_all(A)  # drain

    print("Finished sending 20 messages.")
    print("Now connecting NEW client to fetch history.\n")

    # Connect new client to trigger history delivery
    B = make_client()
    send(B, "conn$HistoryTester")
    time.sleep(0.6)

    history_packets = recv_all(B)

    # Filter for [History] packets only
    history_only = [h for h in history_packets if h.startswith("[History]")]

    print("Received History Packets:")
    for h in history_only:
        print("  " + h)

    print("\nTotal history lines received:", len(history_only))

    print("\nExpected:")
    print("  Should be the LAST 15 messages: Message_5 through Message_19\n")

    # Verification
    expected = [f"[History] HistoryMaker: Message_{i}" for i in range(5, 20)]

    if history_only == expected:
        print("RESULT: PASS — History buffer is correct.")
    else:
        print("RESULT: FAIL — History buffer incorrect.")
        print("\nDifferences:")
        print("Expected:", expected)
        print("Got:", history_only)

    # Cleanup
    send(A, "disconn$")
    send(B, "disconn$")
    time.sleep(0.2)

if __name__ == "__main__":
    main()
