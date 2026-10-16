#!/usr/bin/env python3
"""Recover PS2 IOP ELF files from Final Fantasy XI/PlayOnline encrypted ERX wrappers.
Algorithm reversed from SQIOPMEM.IRX .text+0x94 and .text+0xC4.
Usage: python tools/recover-ffxi-erx-20261016-v1.py INPUT_DIR OUTPUT_DIR
"""
import sys
import struct
from pathlib import Path

def recover(data):
    if len(data) < 68:
        raise ValueError("too short")
    b = bytearray(data)
    for i in range(len(b) - 1, 0, -1):
        b[i] ^= b[i - 1]
    state = len(b)
    for i in range(len(b)):
        state = (state * 0x41C64E6B + 0x3021) & 0xFFFFFFFF
        b[i] ^= (state >> 16) & 0xFF
    filename = bytes(b[4:16]).split(b"\x00", 1)[0].decode("ascii")
    result = bytes(b[16:])
    if not filename.lower().endswith(".irx") or any(c in filename for c in "/\\:"):
        raise ValueError("invalid IRX name after decode")
    if result[:7] != b"\x7fELF\x01\x01\x01" or struct.unpack_from("<HH", result, 16) != (0xFF80, 8):
        raise ValueError("not an IOP MIPS ELF after decode")
    shoff = struct.unpack_from("<I", result, 32)[0]
    shsize, shnum = struct.unpack_from("<HH", result, 46)
    if not shsize or not shnum or shoff + shsize * shnum > len(result):
        raise ValueError("out-of-range ELF section headers")
    return filename, result

def main():
    if len(sys.argv) != 3:
        raise SystemExit("Usage: recover-ffxi-erx-20261016-v1.py INPUT_DIR OUTPUT_DIR")
    source, target = map(Path, sys.argv[1:])
    inputs = sorted(source.glob("*.ERX"))
    if not inputs:
        raise SystemExit("No ERX files found")
    target.mkdir(parents=True, exist_ok=True)
    for p in inputs:
        name, elf = recover(p.read_bytes())
        dst = target / (p.stem + "__" + name)
        if dst.exists():
            raise SystemExit("Refusing to overwrite " + str(dst))
        dst.write_bytes(elf)
        print(f"{p.name} -> {dst.name} ({len(elf)} bytes)")
if __name__ == "__main__":
    main()
