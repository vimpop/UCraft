#!/usr/bin/env python3
"""Exercise the real Linux server without an account or external services."""

import argparse
import json
from pathlib import Path
import re
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def numeric_define(config, name):
    match = re.search(r"^\s*#define\s+" + re.escape(name) + r"\s+(\d+)\b", config, re.M)
    require(match is not None, f"Missing numeric {name} in config.h")
    return int(match.group(1))


def varint(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def packet(body):
    return varint(len(body)) + body


def receive_exact(sock, length):
    result = bytearray()
    deadline = time.monotonic() + 3
    while len(result) < length:
        remaining = deadline - time.monotonic()
        require(remaining > 0, "Timed out reading a packet")
        sock.settimeout(remaining)
        part = sock.recv(length - len(result))
        require(part, "Server closed the connection before completing a packet")
        result.extend(part)
    return bytes(result)


def receive_varint(sock):
    value = 0
    for index in range(5):
        byte = receive_exact(sock, 1)[0]
        value |= (byte & 127) << (index * 7)
        if not byte & 128:
            return value
    raise RuntimeError("Oversized VarInt in server response")


def decode_varint(data, offset=0):
    value = 0
    for index in range(5):
        require(offset < len(data), "Truncated VarInt in server response")
        byte = data[offset]
        offset += 1
        value |= (byte & 127) << (index * 7)
        if not byte & 128:
            return value, offset
    raise RuntimeError("Oversized VarInt in server response")


def receive_packet(sock):
    length = receive_varint(sock)
    require(0 < length <= 1024 * 1024, f"Invalid packet length: {length}")
    return receive_exact(sock, length)


def connect_when_ready(process, port):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        require(process.poll() is None, f"Server exited during startup: {process.returncode}")
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=0.25)
        except (ConnectionRefusedError, TimeoutError):
            time.sleep(0.05)
    raise RuntimeError("Server did not listen within 10 seconds")


def query(sock, port, protocol, max_players, payload):
    host = b"localhost"
    handshake = b"\x00" + varint(protocol) + varint(len(host)) + host + struct.pack(">H", port) + b"\x01"
    request = packet(handshake) + packet(b"\x00")
    sock.sendall(request)
    body = receive_packet(sock)
    packet_id, offset = decode_varint(body)
    require(packet_id == 0, f"Expected status response, got packet {packet_id}")
    length, offset = decode_varint(body, offset)
    require(offset + length == len(body), "Incorrect status JSON length")
    status = json.loads(body[offset:].decode("utf-8"))
    require(status["version"]["protocol"] == protocol, f"Wrong protocol: {status}")
    require(isinstance(status["version"]["name"], str), "Missing version name")
    require(isinstance(status["description"], str), "Missing server description")
    require(status["players"]["max"] == max_players, f"Wrong player limit: {status}")
    require(status["players"]["online"] == 0, f"Unexpected active players: {status}")
    sock.sendall(packet(b"\x01" + payload))
    pong = receive_packet(sock)
    require(pong == b"\x01" + payload, f"Ping payload was not echoed: {pong.hex()}")


def run(args):
    config = args.config.read_text()
    port = numeric_define(config, "PORT")
    protocol = numeric_define(config, "PROTOCOL_VERSION")
    max_players = numeric_define(config, "MAX_PLAYERS")
    # Never accidentally test a server already running on the configured port.
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(("127.0.0.1", port))

    with args.log.open("wb") as log, tempfile.TemporaryDirectory(prefix="ucraft-smoke-") as cwd:
        process = subprocess.Popen([str(args.server.resolve())], cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
        try:
            for payload in (b"\x01\x23\x45\x67\x89\xab\xcd\xef", b"\xfe\xdc\xba\x98\x76\x54\x32\x10"):
                with connect_when_ready(process, port) as sock:
                    query(sock, port, protocol, max_players, payload)
            require(process.poll() is None, "Server exited after serving status/ping")
            process.send_signal(signal.SIGINT)
            require(process.wait(timeout=5) == 0, "Server failed during graceful shutdown")
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
    print("PASS status/ping on two connections and graceful shutdown")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    args = parser.parse_args()
    try:
        run(args)
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        if args.log.exists():
            print(args.log.read_text(errors="replace")[-16000:], file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
