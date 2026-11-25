"""Tiny OSC client for poking a running zynaddsubfx over UDP.

Usage: python tools/osc_probe.py PORT /path [type:value ...]
  type is i (int), f (float), s (string), T or F (no value).
Prints every reply that arrives within half a second.
Also importable: send(port, path, *args) returns the replies.
"""
import socket
import struct
import sys


def _pad(b):
    return b + b'\0' * (4 - len(b) % 4)


def encode(path, *args):
    tags = ','
    data = b''
    for a in args:
        if a is True:
            tags += 'T'
        elif a is False:
            tags += 'F'
        elif isinstance(a, int):
            tags += 'i'
            data += struct.pack('>i', a)
        elif isinstance(a, float):
            tags += 'f'
            data += struct.pack('>f', a)
        else:
            tags += 's'
            data += _pad(str(a).encode('utf-8'))
    return _pad(path.encode()) + _pad(tags.encode()) + data


def _string(buf, pos):
    end = buf.index(b'\0', pos)
    s = buf[pos:end].decode('utf-8', 'replace')
    return s, (end + 4) & ~3


def decode(buf):
    path, pos = _string(buf, 0)
    if pos >= len(buf):
        return path, []
    tags, pos = _string(buf, pos)
    out = []
    for t in tags[1:]:
        if t == 'i':
            out.append(struct.unpack_from('>i', buf, pos)[0]); pos += 4
        elif t == 'f':
            out.append(struct.unpack_from('>f', buf, pos)[0]); pos += 4
        elif t == 's':
            v, pos = _string(buf, pos); out.append(v)
        elif t == 'b':
            n = struct.unpack_from('>i', buf, pos)[0]; pos += 4
            out.append(buf[pos:pos + n]); pos += (n + 3) & ~3
        elif t in 'TF':
            out.append(t == 'T')
        elif t == 'c':
            out.append(struct.unpack_from('>i', buf, pos)[0]); pos += 4
        else:
            out.append('?' + t)
    return path, out


def send(port, path, *args, wait=0.5, sock=None):
    own = sock is None
    if own:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('127.0.0.1', 0))
    sock.settimeout(wait)
    sock.sendto(encode(path, *args), ('127.0.0.1', port))
    replies = []
    try:
        while True:
            data, _ = sock.recvfrom(65536)
            replies.append(decode(data))
    except socket.timeout:
        pass
    if own:
        sock.close()
    return replies


def _parse(arg):
    t, _, v = arg.partition(':')
    return {'i': int, 'f': float, 's': str}.get(t, lambda _: t == 'T')(v)


if __name__ == '__main__':
    port = int(sys.argv[1])
    for path, args in send(port, sys.argv[2], *[_parse(a) for a in sys.argv[3:]]):
        print(path, args)
