import socket
import time

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000

# Fn for printing section banners
def banner(title):
    print("\n" + "=" * 60)
    print(title)
    print("=" * 60)

# Clear any queued UDP packets so each test only reads responses related to the current command
def drain_socket(sock):
    """Remove any queued packets so we read only new responses."""
    sock.setblocking(False)
    try:
        while True:
            sock.recvfrom(8192)
    except:
        pass
    sock.setblocking(True)

# Send a command to the server and print the first reply (if any)
def send_recv(sock, msg, timeout=1.0):
    """Send a request and return the first response (if any)."""
    drain_socket(sock)

    print(f"→ Sending: {msg}")
    sock.sendto(msg.encode(), (SERVER_IP, SERVER_PORT))

    sock.settimeout(timeout)
    try:
        data, _ = sock.recvfrom(8192)
        reply = data.rstrip(b"\x00").decode(errors="replace").strip()
        print(f"← Received:")
        print("  ", reply)
    except socket.timeout:
        print("← Received:")
        print("   <NO RESPONSE>")
    finally:
        sock.settimeout(None)

    time.sleep(0.3)


def main():
    banner("BASIC FUNCTIONALITY TEST")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))

    print(f"Client bound to UDP port {sock.getsockname()[1]}")

    banner("1. CONNECT")
    send_recv(sock, "conn$ Alice")

    banner("2. BROADCAST (say$)")
    send_recv(sock, "say$ Hello world")

    banner("3. PRIVATE MESSAGE (sayto$)")
    send_recv(sock, "sayto$ Bob How are you?")

    banner("4. MUTE")
    send_recv(sock, "mute$ Charlie")

    banner("5. UNMUTE")
    send_recv(sock, "unmute$ Charlie")

    banner("6. RENAME")
    send_recv(sock, "rename$ Alice123")

    banner("7. DISCONNECT")
    send_recv(sock, "disconn$")

    banner("TEST COMPLETE")
    print("Inspect output above to confirm correct behaviour.\n")


if __name__ == "__main__":
    main()
