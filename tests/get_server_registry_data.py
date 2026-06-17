import socket

def rpkt() -> bytes:
    global sock

    sz = 0
    pos = 0

    while True:
        byte = int.from_bytes(sock.recv(1))
        sz|=(byte&0x7f)<<pos
        if (byte&0x80) == 0:break

        pos+=7
    return sock.recv(sz)

def wpkt(pkt: bytes) -> None:
    global sock

    value = len(pkt)
    while True:
        if ((value & ~0x7F) == 0):
            sock.send((value & 0xFF).to_bytes(1))
            break
        sock.send(((value & 0x7F) | 0x80).to_bytes(1))
        value >>= 7
    sock.send(pkt)

def vari32asb(value: int) -> bytes:
    if value < 0:
        raise NotImplementedError
    ret=b''
    while True:
        if (value & ~0x7f)==0:
            ret += (value&0xff).to_bytes(1)
            return ret
        ret += ((value&0x7f)|0x80).to_bytes(1)
        value >>= 7

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 25565))

# handshake
wpkt(b'\x00' + vari32asb(775) + b'\x04addr\x00\x00\x02')

# login start
wpkt(b'\x00\x08KlasterK')

# login success
while rpkt()[0] != 0x02: pass

# login ack
wpkt(b'\x03')


