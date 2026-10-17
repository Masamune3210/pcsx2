#!/usr/bin/env python3
"""Read-only extraction of one HDD Utility 0x1A0 header from MGRAW logs.

A valid extraction requires one SCmd90 followed by 26 SCmd8D writes, an
exactly matching pre-8F MG_STAGED_BUFFER, and a 0x1A0 internal header length.
No encryption or fabricated authentication is performed.
"""
import argparse
import hashlib
import re
from pathlib import Path

CALL = re.compile(r"\[MGFLOW\] CALL scmd=([0-9A-Fa-f]{2})\b")
REQ = re.compile(r"\[MGRAW\] SCMD_REQUEST \+0x0000: ((?:[0-9A-Fa-f]{2}(?: |$))+)")
STAGE = re.compile(r"\[MGRAW\] MG_STAGED_BUFFER \+0x([0-9A-Fa-f]{4}): ((?:[0-9A-Fa-f]{2}(?: |$))+)")

def extract(text):
    lines = text.splitlines()
    staged = [i for i, s in enumerate(lines) if "[MGRAW] MG_STAGED_BUFFER length=0x1A0" in s]
    if len(staged) != 1:
        raise ValueError("expected exactly one 0x1A0 staged-buffer dump")
    end = staged[0]
    output = bytearray()
    for s in lines[end + 1:end + 27]:
        m = STAGE.search(s)
        if not m or int(m[1], 16) != len(output):
            raise ValueError("missing or out-of-order staged-buffer row")
        b = bytes.fromhex(m[2])
        if len(b) != 16: raise ValueError("staged row not 16 bytes")
        output.extend(b)
    if len(output) != 416: raise ValueError("incomplete staged header")
    starts = [i for i,s in enumerate(lines[:end]) if "[MGFLOW] CALL scmd=90 " in s]
    if not starts: raise ValueError("no 0x90 start")
    start = starts[-1]
    calls = [(start+i,m[1].upper()) for i,s in enumerate(lines[start:end]) if (m := CALL.search(s))]
    if len(calls)!=28 or calls[0][1]!="90" or calls[-1][1]!="8F" or any(c!="8D" for _,c in calls[1:-1]):
        raise ValueError("expected exactly 90, 26x8D, 8F")
    chunks=[]
    for j,(pos,opcode) in enumerate(calls[:-1]):
        until=calls[j+1][0]
        matches=[m[1] for s in lines[pos:until] if (m := REQ.search(s))]
        if len(matches)!=1: raise ValueError("expected one request per command")
        raw=bytes.fromhex(matches[0])
        if j==0 and raw!=bytes.fromhex("00 a0 01 00 00"): raise ValueError("wrong 90 parameters")
        if j>0 and len(raw)!=16: raise ValueError("wrong 8D size")
        if not any("SCMD_RESPONSE +0x0000: 00" in s for s in lines[pos:until]):
            raise ValueError("command rejected or response missing")
        chunks.append(raw)
    if b"".join(chunks[1:])!=output: raise ValueError("8D requests and staged bytes differ")
    if output[0x14:0x16]!=b"\xa0\x01": raise ValueError("wrong embedded size")
    return bytes(output)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("log",type=Path)
    p.add_argument("output",type=Path)
    a=p.parse_args()
    data=extract(a.log.read_text(encoding="utf-8-sig",errors="replace"))
    a.output.write_bytes(data)
    print(f"Verified {len(data)} bytes; SHA256 {hashlib.sha256(data).hexdigest()}")
if __name__=="__main__": main()
