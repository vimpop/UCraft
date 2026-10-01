"""Decode the server's real packet captures using Python's independent zlib."""
import argparse
from pathlib import Path
import zlib


def varint(data, offset):
    value = 0
    for index in range(5):
        byte = data[offset]
        offset += 1
        value |= (byte & 127) << (7 * index)
        if not byte & 128:
            return value, offset
    raise AssertionError("invalid VarInt")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    packets = sorted(args.directory.glob("compression-*.bin"))
    assert len(packets) == 3, "missing threshold boundary captures"
    for path in packets:
        size = int(path.stem.split("-")[1])
        data = path.read_bytes()
        length, offset = varint(data, 0)
        assert length == len(data) - offset
        expanded, offset = varint(data, offset)
        payload = data[offset:]
        if expanded:
            stream = zlib.decompressobj()
            payload = stream.decompress(payload) + stream.flush()
            assert stream.eof and not stream.unused_data and not stream.unconsumed_tail
            assert expanded == len(payload)
        assert payload == bytes(index % 251 for index in range(size)), path.name
    print("PASS: compression threshold boundaries")


if __name__ == "__main__":
    main()
