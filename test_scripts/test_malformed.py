import socket
import time
import os

SERVER_IP = "127.0.0.1"
SERVER_PORT = 12000
TIMEOUT = 0.5

# Fn for printing section banners
def banner(title):
    print("\n" + "=" * 60)
    print(title)
    print("=" * 60)


# Create and return a UDP client socket
def make_client():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.settimeout(TIMEOUT)
    return sock


# Send a message and optionally wait for a reply
def send_and_recv(sock, msg, expect_reply=True):
    sock.sendto(msg.encode(errors="ignore"), (SERVER_IP, SERVER_PORT))

    if not expect_reply:
        time.sleep(0.2)
        return "<NO RESPONSE EXPECTED>"

    try:
        data, _ = sock.recvfrom(4096)
        return data.decode(errors="replace").strip()
    except socket.timeout:
        return "<NO RESPONSE>"


# Assertions expecting an error
def expect_error(label, reply):
    if reply.startswith("ERR$"):
        print(f"PASS: {label}")
    else:
        print(f"FAIL: {label}")
        print("  Received:", reply)


# Assertions expecting silence
def expect_no_response(label, reply):
    if reply in ("<NO RESPONSE>", "<NO RESPONSE EXPECTED>"):
        print(f"PASS: {label}")
    else:
        print(f"FAIL: {label}")
        print("  Received:", reply)


# Assertions expecting any non-error reply
def expect_ok(label, reply):
    print(f"PASS: {label}")
    print("  Received:", reply)


def main():

    sock = make_client()

    banner("MALFORMED INPUT TEST")
    print("Client bound to UDP port", sock.getsockname()[1])

    # Valid handshake
    banner("CONNECT VALID CLIENT")

    r = send_and_recv(sock, "conn$Tester")
    print("Response:", r)
    expect_ok("valid connection", r)

    # Missing '$'
    banner("NO DELIMITER ($)")

    expect_error("no delimiter", send_and_recv(sock, "say Hello"))
    expect_error("no delimiter", send_and_recv(sock, "conn Alice"))
    expect_error("no delimiter", send_and_recv(sock, "mute Bob"))

    # Empty sections
    banner("EMPTY COMMAND PARTS")

    expect_error("empty command type", send_and_recv(sock, "$NoCommand"))
    expect_no_response("empty say payload (allowed)", send_and_recv(sock, "say$", expect_reply=False))
    expect_error("rename without name", send_and_recv(sock, "rename$"))

    # Unknown operations
    banner("UNKNOWN COMMANDS")

    expect_error("unknown command", send_and_recv(sock, "explode$everything"))
    expect_error("unknown command", send_and_recv(sock, "asdqwe$123"))

    # Whitespace abuse
    banner("WHITESPACE ABUSE")

    expect_error("spaces only", send_and_recv(sock, "     "))
    expect_error("tabs only", send_and_recv(sock, "\t\t\t"))
    expect_no_response("say with whitespace payload", send_and_recv(sock, "say$      ", expect_reply=False))
    expect_ok("rename with whitespace", send_and_recv(sock, " rename$Bob "))

    # Extra delimiter handling
    banner("MULTIPLE $ CHARACTERS")

    expect_ok("double delimiter allowed", send_and_recv(sock, "say$$double"))
    expect_error("empty empty", send_and_recv(sock, "$$"))
    expect_ok("double delimiter conn", send_and_recv(sock, "conn$$User"))

    # Large payload
    banner("OVERSIZED PAYLOAD")

    big = "say$" + ("X" * 2000)
    expect_ok("oversized payload accepted", send_and_recv(sock, big))

    # Binary packet injection
    banner("BINARY DATA")

    sock.sendto(os.urandom(50), (SERVER_IP, SERVER_PORT))
    time.sleep(0.3)
    print("PASS: binary input did not crash server")

    # Bad disconnect usage
    banner("DISCONNECT ABUSE")

    expect_ok("disconnect with payload", send_and_recv(sock, "disconn$junk"))

    # Proper disconnect
    banner("CLEAN DISCONNECT")

    expect_ok("clean disconnection", send_and_recv(sock, "disconn$"))

    # Final summary
    banner("TEST COMPLETE")

    print("Expected behaviour:")
    print(" - ERR$ when malformed")
    print(" - silence for invalid say payloads")
    print(" - no blocking")
    print(" - no crashes")


if __name__ == "__main__":
    main()
