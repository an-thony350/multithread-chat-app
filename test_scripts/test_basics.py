# test_basic.py
import socket
import time

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000

def send_recv(sock, msg, timeout=0.5):
    # Clear the socket
    sock.setblocking(False)
    while True:
        try:
            sock.recvfrom(8192)
        except:
            break
    sock.setblocking(True)

    # Send command
    sock.sendto(msg.encode(), (SERVER_IP, SERVER_PORT))

    # Get FIRST incoming packet
    sock.settimeout(timeout)
    try:
        data, _ = sock.recvfrom(8192)
        return data.rstrip(b"\x00").decode(errors="replace").strip()
    except socket.timeout:
        return "<NO RESPONSE>"



def banner(title):
    print("\n" + "="*60)
    print(title)
    print("="*60)

def main():
    banner("BASIC BEHAVIOUR TEST")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))

    banner("1. CONNECT TEST")
    print("Sending: conn$Alice")
    print("Response:", send_recv(sock, "conn$Alice"))
    time.sleep(0.2)

    banner("2. BROADCAST (say$)")
    print("Sending: say$Hello world")
    print("Response:", send_recv(sock, "say$Hello world"))
    time.sleep(0.2)

    banner("3. PRIVATE MESSAGE (sayto$Bob)")
    print("Sending: sayto$Bob How are you?")
    print("Response:", send_recv(sock, "sayto$Bob How are you?"))
    time.sleep(0.2)

    banner("4. MUTE")
    print("Sending: mute$Charlie")
    print("Response:", send_recv(sock, "mute$Charlie"))
    time.sleep(0.2)

    banner("5. UNMUTE")
    print("Sending: unmute$Charlie")
    print("Response:", send_recv(sock, "unmute$Charlie"))
    time.sleep(0.2)

    banner("6. RENAME")
    print("Sending: rename$Alice123")
    print("Response:", send_recv(sock, "rename$Alice123"))
    time.sleep(0.2)

    banner("7. DISCONNECT")
    print("Sending: disconn$")
    print("Response:", send_recv(sock, "disconn$"))
    time.sleep(0.2)

    banner("TEST COMPLETE")
    print("All tests executed. Check output above for server behaviour.\n")

if __name__ == "__main__":
    main()
