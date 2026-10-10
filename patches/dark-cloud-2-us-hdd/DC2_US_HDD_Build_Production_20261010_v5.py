#!/usr/bin/env python3
"""Build and strictly verify the lean US Dark Cloud 2 HDD loader.
No debug instrumentation, no game/HDD modification. Requires clang, ld.lld,
llvm-objcopy for --build; --audit needs only Python.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile

BASE = 0x01FE8000
SIZE = 488
SHA256 = "bea8b5981c092fa501acee8884db834c1605b52fe0110a5b13250963efda48d4"
SOURCE = Path(__file__).with_name("DC2_US_HDD_Loader_Production_20261010_v5.S")
RELEASE = Path(__file__).with_name("DC2_US_HDD_Production_20261010_v5.pnach")
GROUP = "[Dark Cloud 2 - HDD Support]"
GATE = (1, 0x0029F2EC, 0)
HOOKS = {0x0015C398: 0x087FA000, 0x0015C374: 0x0C0570E6}
CAPACITY = {0x0031C4BC: 0x2A42000C, 0x0031BF40: 0x2A220680}
LINE = re.compile(r"patch=([01]),EE,([0-9A-Fa-f]{8}),word,([0-9A-Fa-f]{8})\Z")
PATHS = (
    b"cdrom0:\\MODULES\\DEV9.IRX;1\x00",
    b"cdrom0:\\MODULES\\ATAD.IRX;1\x00",
    b"cdrom0:\\MODULES\\HDD.IRX;1\x00",
    b"cdrom0:\\MODULES\\PFS.IRX;1\x00",
)
ARGS = (b"-o\x002\x00-n\x0020\x00", b"-m\x002\x00-o\x0016\x00-n\x0040\x00")
LOAD_API, INIT_API, ORIGINAL_API = 0x00117D58, 0x00114008, 0x00148F70

def dec_addiu(word: int, dest: int, src: int) -> int:
    if word >> 26 != 9 or word >> 21 & 31 != src or word >> 16 & 31 != dest:
        raise ValueError("Expected ADDIU register assignment")
    value = word & 0xFFFF
    return value - 0x10000 if value & 0x8000 else value

def dec_pointer(lui: int, addiu: int, reg: int) -> int:
    if lui >> 26 != 15 or lui >> 16 & 31 != reg or lui >> 21 & 31 != 0:
        raise ValueError("Expected LUI address load")
    return (((lui & 0xFFFF) << 16) + dec_addiu(addiu, reg, reg)) & 0xFFFFFFFF

def target_branch(pc: int, word: int) -> int:
    off = word & 0xFFFF
    if off >= 0x8000: off -= 0x10000
    return pc + 4 + off * 4

def load_binary(src: Path = SOURCE) -> bytes:
    with tempfile.TemporaryDirectory() as directory:
        d = Path(directory)
        obj, exe, raw = d / "loader.o", d / "loader.elf", d / "loader.bin"
        subprocess.run(["clang", "-target", "mipsel-unknown-elf", "-march=mips3",
                        "-c", str(src), "-o", str(obj)], check=True)
        subprocess.run(["ld.lld", "-m", "elf32ltsmip", "--entry=_start", "-Ttext",
                        f"0x{BASE:08X}", "-o", str(exe), str(obj)], check=True)
        subprocess.run(["llvm-objcopy", "--only-section=.text", "-O", "binary",
                        str(exe), str(raw)], check=True)
        return raw.read_bytes()

def inspect(payload: bytes) -> None:
    if len(payload) != SIZE or hashlib.sha256(payload).hexdigest() != SHA256:
        raise ValueError("Unexpected loader binary: source or patch no longer matches lean candidate")
    for marker in (b"DC2HDDv1", b"DC2HDDv2", b"DC2HDD", b"dc2_stage", b"dc2_failure"):
        if marker in payload: raise ValueError("Debug marker leaked into production binary")
    for name in PATHS + ARGS:
        if payload.count(name) != 1:
            raise ValueError(f"Missing/duplicate path or arguments: {name!r}")
    words = [w[0] for w in struct.iter_unpack("<I", payload)]
    if words[:11] != [
        0x27BDFF40, 0xFFBF00B8, 0xFFBC00B0, 0xFFB000A8, 0xFFB100A0,
        0xFFB20098, 0xFFB30090, 0xFFA40088, 0xFFA50080, 0xFFA60078, 0xFFA70070,
    ]:
        raise ValueError("Original preserved EE register frame was modified")
    # Original InitCDFile epilogue and saved registers; one contiguous sequence.
    footer = [
        0xDFA40088, 0xDFA50080, 0xDFA60078, 0xDFA70070,
        0x0C0523DC, 0,
        0xDFB30090, 0xDFB20098, 0xDFB100A0, 0xDFB000A8,
        0xDFBC00B0, 0xDFBF00B8, 0x27BD00C0, 0x03E00008, 0,
    ]
    starts = [i for i in range(len(words) - len(footer) + 1) if words[i:i + len(footer)] == footer]
    if len(starts) != 1: raise ValueError("Original InitCDFile continuation is missing")
    original_pc = BASE + 4 * starts[0]
    # Four IOP module loads and exactly one sceFsInit and InitCDFile call.
    jal_targets = [((BASE+4*i+4) & 0xF0000000) | ((w & 0x03FFFFFF)<<2)
                   for i,w in enumerate(words) if w >> 26 == 3]
    if jal_targets != [LOAD_API]*4 + [INIT_API, ORIGINAL_API]:
        raise ValueError(f"Wrong original API call sequence: {jal_targets!r}")
    load_indices = [i for i,w in enumerate(words) if w == (0x0C000000 | (LOAD_API >> 2))]
    for i, (path, args_size, args) in zip(load_indices,
                                          zip(PATHS, (0,0,11,17), (None,None,*ARGS))):
        # Each call must use exactly the corresponding module path and argument buffer.
        if args is None:
            pre = words[i-6:i]
            if len(pre)!=6 or pre[0]!=0xAFA00020 or dec_pointer(pre[1],pre[2],4)!=BASE+payload.index(path):
                raise ValueError("Incorrect DEV9/ATAD setup")
            if dec_addiu(pre[3],5,0)!=0 or pre[4]!=0x00003025 or pre[5]!=0x27A70020:
                raise ValueError("Incorrect DEV9/ATAD argument/register setup")
        else:
            pre = words[i-7:i]
            if len(pre)!=7 or pre[0]!=0xAFA00020 or dec_pointer(pre[1],pre[2],4)!=BASE+payload.index(path):
                raise ValueError("Incorrect HDD/PFS path setup")
            if dec_addiu(pre[3],5,0)!=args_size or dec_pointer(pre[4],pre[5],6)!=BASE+payload.index(args) or pre[6]!=0x27A70020:
                raise ValueError("Incorrect HDD/PFS module startup arguments")
        # Reject startup failures as in the tested loader, and fall through to InitCDFile.
        expected = (0x00000000, 0x8FB90020, 0x24080001, None, 0x00000000)
        if words[i+1] != 0 or words[i+2] >> 26 != 1 or words[i+2] >> 21 & 31 != 2 or words[i+2] >> 16 & 31 != 0:
            raise ValueError("Missing negative module-ID check")
        if target_branch(BASE + 4*(i+2),words[i+2]) != original_pc or words[i+3] != 0:
            raise ValueError("Module-ID failure branch does not reach original init")
        if words[i+4:i+6] != [0x8FB90020, 0x24080001] or words[i+6] >> 26 != 4 or words[i+7]!=0:
            raise ValueError("Missing start-result failure check")
        if words[i+6] >> 21 & 31 != 25 or words[i+6] >> 16 & 31 != 8:
            raise ValueError("Start-result check registers differ")
        if target_branch(BASE + 4*(i+6),words[i+6]) != original_pc:
            raise ValueError("Start-result failure branch does not reach original init")
    if words[starts[0]-2:starts[0]] != [0x0C045002, 0]:
        raise ValueError("sceFsInit must precede original InitCDFile")
    print(f"PASS: {SIZE} bytes, four checked IRX starts, sceFsInit, original InitCDFile; no diagnostics")

def expected(payload: bytes) -> dict[int,tuple[int,int]]:
    inspect(payload)
    e = {BASE+off: (0,struct.unpack_from("<I",payload,off)[0]) for off in range(0,SIZE,4)}
    for a,v in HOOKS.items(): e[a]=(0,v)
    for a,v in CAPACITY.items(): e[a]=(0,v)
    e[GATE[1]]=(GATE[0],GATE[2])
    return e

def render(payload: bytes) -> str:
    expected(payload)
    result = [
        "// Dark Cloud 2 (USA) v2.00 | SCUS_972.13 | CRC 1DF41F33",
        "// Requires a PS2 HDD and the original disc MODULES IRX files.",
        GROUP,
        "description=Enable native US HDD installation and loading (1664 MiB).",
        "patch=1,EE,0029F2EC,word,00000000",
    ]
    for off in range(0,SIZE,4):
        result.append(f"patch=0,EE,{BASE+off:08X},word,{struct.unpack_from('<I',payload,off)[0]:08X}")
    result.extend([f"patch=0,EE,{a:08X},word,{v:08X}" for a,v in HOOKS.items()])
    result.extend([f"patch=0,EE,{a:08X},word,{v:08X}" for a,v in CAPACITY.items()])
    return "\n".join(result) + "\n"

def parse(text: str) -> dict[int,tuple[int,int]]:
    result = {}
    groups = []
    for index, raw in enumerate(text.splitlines(),1):
        s = raw.strip()
        if not s or s.startswith(("//","#",";")) or s.startswith("description="):
            continue
        if s.startswith("[") and s.endswith("]"):
            groups.append(s)
            continue
        m = LINE.fullmatch(s)
        if not m: raise ValueError(f"Line {index}: unrecognized directive {s!r}")
        mode, a, v = m.groups()
        addr=int(a,16)
        if addr in result: raise ValueError(f"Line {index}: duplicate address {addr:08X}")
        result[addr]=(int(mode),int(v,16))
    if groups!=[GROUP]: raise ValueError(f"Expected one HDD feature group, got {groups!r}")
    return result

def audit(text: str, payload: bytes) -> None:
    got=parse(text)
    ref=expected(payload)
    if got!=ref:
        missing=sorted(ref.keys()-got.keys())
        extra=sorted(got.keys()-ref.keys())
        changed=sorted(k for k in ref.keys()&got.keys() if got[k]!=ref[k])
        raise ValueError(f"Patch differs: missing={missing} extra={extra} changed={changed}")
    print(f"PASS: {len(got)} patch words, exact production loader SHA256={SHA256}")

def main() -> None:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build",action="store_true")
    ap.add_argument("--audit",type=Path)
    ap.add_argument("--source",type=Path,default=SOURCE)
    ap.add_argument("--output",type=Path,default=RELEASE)
    a=ap.parse_args()
    if not a.build and not a.audit: ap.error("Specify --build and/or --audit")
    if a.build:
        blob=load_binary(a.source)
        generated=render(blob)
        if a.output.exists(): raise FileExistsError(f"Refusing overwrite: {a.output}")
        a.output.write_text(generated,encoding="utf-8",newline="\n")
        print(f"CREATED {a.output}")
    if a.audit:
        contents=a.audit.read_text(encoding="utf-8-sig")
        edits=parse(contents)
        data=b"".join(struct.pack("<I",edits.get(BASE+i,(0,0))[1]) for i in range(0,SIZE,4))
        audit(contents,data)

if __name__=="__main__":
    main()
