#!/usr/bin/env python3
"""Extract the first complete 0x400-byte SCmd8F content input from an existing PCSX2 MGRAW log.

This never downloads or embeds any copyrighted content and refuses to overwrite output.
"""
import argparse
import hashlib
import re
from pathlib import Path

CALL = re.compile(r"\[MGFLOW\] CALL scmd=8F\b.*\bdataType=0\b.*\bbuffered=0x400\b")
START = re.compile(r"\[MGRAW\] MG_STAGED_BUFFER length=0x400\b")
ROW = re.compile(r"\[MGRAW\] MG_STAGED_BUFFER \+0x([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2}(?:\s+|$))+)")
def extract(text):
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if not CALL.search(line):
            continue
        for j in range(i+1, min(i+6,len(lines))):
            if not START.search(lines[j]):
                continue
            data = bytearray()
            for following in lines[j+1:]:
                m = ROW.search(following)
                if not m:
                    break
                offset = int(m.group(1),16)
                chunk = bytes.fromhex(m.group(2))
                if offset != len(data) or len(data)+len(chunk)>0x400:
                    raise ValueError("Noncontiguous or oversized MGRAW data")
                data.extend(chunk)
            if len(data)==0x400:
                return bytes(data)
            raise ValueError(f"Incomplete first 0x400-byte staging: {len(data)} bytes")
    raise ValueError("No 0x400-byte SCmd8F content staging found in this log")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("existing_mgraw_log",type=Path)
    parser.add_argument("output_ciphertext",type=Path)
    args = parser.parse_args()
    data = extract(args.existing_mgraw_log.read_text(encoding="utf-8",errors="replace"))
    with args.output_ciphertext.open("xb") as output:
        output.write(data)
    print(f"Extracted {len(data)} bytes; SHA256={hashlib.sha256(data).hexdigest()}")
    print(f"Private local fixture written to {args.output_ciphertext} (do not commit)")

if __name__=="__main__":
    main()
