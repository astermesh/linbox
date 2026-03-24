#!/usr/bin/env python3
import argparse
import socket
import struct
import sys

SBP_VERSION = 1
SBP_MSG_HELLO = 1
SBP_MSG_ACK = 2
SBP_MSG_SET_TIME = 3
SBP_MSG_SET_SEED = 4
SBP_HEADER = struct.Struct('<BBHI')


def frame(msg_type: int, payload: bytes = b'', flags: int = 0) -> bytes:
    return SBP_HEADER.pack(SBP_VERSION, msg_type, flags, len(payload)) + payload


def recv_exact(sock: socket.socket, n: int) -> bytes:
    data = bytearray()
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise RuntimeError('unexpected EOF')
        data.extend(chunk)
    return bytes(data)


def recv_frame(sock: socket.socket):
    header = recv_exact(sock, SBP_HEADER.size)
    version, msg_type, flags, payload_len = SBP_HEADER.unpack(header)
    payload = recv_exact(sock, payload_len)
    return version, msg_type, flags, payload


def send_and_expect_ack(sock: socket.socket, msg_type: int, payload: bytes = b''):
    sock.sendall(frame(msg_type, payload))
    version, ack_type, _flags, _payload = recv_frame(sock)
    if version != SBP_VERSION or ack_type != SBP_MSG_ACK:
        raise RuntimeError(f'expected ACK, got version={version} type={ack_type}')


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--socket', required=True)
    sub = parser.add_subparsers(dest='command', required=True)

    set_time = sub.add_parser('set-time')
    set_time.add_argument('--seconds', type=int, required=True)
    set_time.add_argument('--nanos', type=int, default=0)

    set_seed = sub.add_parser('set-seed')
    set_seed.add_argument('--seed', type=int, required=True)

    args = parser.parse_args()

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(args.socket)
        send_and_expect_ack(sock, SBP_MSG_HELLO)

        if args.command == 'set-time':
            payload = struct.pack('<qi', args.seconds, args.nanos)
            send_and_expect_ack(sock, SBP_MSG_SET_TIME, payload)
            print(f'set-time seconds={args.seconds} nanos={args.nanos}')
            return 0
        if args.command == 'set-seed':
            payload = struct.pack('<Q', args.seed)
            send_and_expect_ack(sock, SBP_MSG_SET_SEED, payload)
            print(f'set-seed seed={args.seed}')
            return 0

    return 1


if __name__ == '__main__':
    sys.exit(main())
