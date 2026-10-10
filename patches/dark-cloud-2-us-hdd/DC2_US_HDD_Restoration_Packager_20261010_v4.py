#!/usr/bin/env python3
"""Auditable, fail-closed packager for Dark Cloud 2 USA 2.00 HDD restoration.

The MIPS payload is intentionally the *v1* loader, whose instruction words are
confirmed in the successful post-install cold-boot PCSX2 log. The v2 loader
has only diagnostic differences but was NOT the bytecode seen in that log.

No game binary, IRX, or HDD image is included or modified by this tool.
Requires clang/ld.lld/llvm-objcopy only when --build is requested.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile

CRC = '1DF41F33'
BASE = 0x01FE8000
PAYLOAD_SIZE = 0x02E0
PAYLOAD_SHA256 = 'b8f2bf8a1a974c6bcc505ac9fbaafbc9f396a665628c47925354c80af4861ef1'
CAPACITY_IMMEDIATES = {
    0x0031C4BC: 0x2A42000C,  # installer slti v0,s2,12
    0x0031BF40: 0x2A220680,  # check slti v0,s1,1664
}
HOOKS = {
    0x0015C398: 0x087FA000,  # j 0x01FE8000, previous instruction at 0x0015C39C is nop
    0x0015C374: 0x0C0570E6,  # jal 0x0015C398, original delay slot stays nop
}
GATE = (1, 0x0029F2EC, 0x00000000)
EXPECTED_PATCHES = (PAYLOAD_SIZE // 4) + len(CAPACITY_IMMEDIATES) + len(HOOKS) + 1
DEFAULT_SOURCE = Path(__file__).with_name('DC2_US_HDD_Loader_KnownGood_20261010_v4.S')
DEFAULT_PATCH = Path(__file__).with_name('DC2_US_HDD_Restoration_Clean_20261010_v4.pnach')
ENTRY = re.compile(r'^patch\s*=\s*([01]),\s*EE,\s*([0-9a-fA-F]{8}),\s*word,\s*([0-9a-fA-F]{1,8})\s*$', re.I | re.M)
LOG_ENTRY = re.compile(r'^\[.*?\] +([01]),EE,word,([0-9a-fA-F]{8}),([0-9a-fA-F]{1,8})\s*$', re.I | re.M)


def words(blob: bytes) -> dict[int, tuple[int, int]]:
    if len(blob) != PAYLOAD_SIZE or hashlib.sha256(blob).hexdigest() != PAYLOAD_SHA256:
        raise ValueError('Loader bytes differ from verified successful v1 payload; refusing to package')
    if blob[0:4] != struct.pack('<I', 0x27BDFF40):
        raise ValueError('Unexpected loader entry instruction')
    for required in [b'DC2HDDv1', b'cdrom0:\\MODULES\\DEV9.IRX;1\0',
                     b'cdrom0:\\MODULES\\ATAD.IRX;1\0',
                     b'cdrom0:\\MODULES\\HDD.IRX;1\0',
                     b'cdrom0:\\MODULES\\PFS.IRX;1\0',
                     b'-o\x002\x00-n\x0020\x00', b'-m\x002\x00-o\x0016\x00-n\x0040\x00']:
        if required not in blob:
            raise ValueError(f'Missing loader signature: {required!r}')
    return {BASE + i: (0, struct.unpack_from('<I', blob, i)[0]) for i in range(0, len(blob), 4)}


def expected(blob: bytes) -> dict[int, tuple[int, int]]:
    d = words(blob)
    d.update({a: (0, v) for a, v in HOOKS.items()})
    d.update({a: (0, v) for a, v in CAPACITY_IMMEDIATES.items()})
    d[GATE[1]] = (GATE[0], GATE[2])
    assert len(d) == EXPECTED_PATCHES
    return d


def parse_pnach(s: str) -> dict[int, tuple[int, int]]:
    """Read the entire patch strictly, including malformed or unsupported write lines.

    The previous audit used ENTRY.findall() alone, which could silently skip
    invalid commands. Here every directive must parse or the audit fails.
    """
    d = {}
    groups = []
    for lineno, raw in enumerate(s.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith(('#', '//', ';')) or line.lower().startswith('description='):
            continue
        if line.startswith('[') and line.endswith(']'):
            groups.append(line)
            continue
        if not line.lower().startswith('patch='):
            raise ValueError(f'Line {lineno}: unexpected .pnach directive {line!r}')
        match = ENTRY.fullmatch(line)
        if not match:
            raise ValueError(f'Line {lineno}: malformed/unsupported patch directive {line!r}')
        mode, addr, word = match.groups()
        addr = int(addr, 16)
        if addr in d:
            raise ValueError(f'Line {lineno}: duplicate EE address 0x{addr:08X}')
        d[addr] = (int(mode), int(word, 16))
    if groups != [
        '[Dark Cloud 2 - Existing HDD Title Menu Gate]',
        '[Dark Cloud 2 - EXPERIMENTAL HDD Driver Loader]',
        '[Dark Cloud 2 - US HDD Installer 1664 MiB Capacity Test]',
    ]:
        raise ValueError(f'Unexpected/changed PCSX2 group names: {groups!r}')
    return d


def audit_text(s: str, blob: bytes, name: str) -> None:
    entries = parse_pnach(s)
    ref = expected(blob)
    if entries != ref:
        missing = sorted(set(ref) - set(entries))
        extra = sorted(set(entries) - set(ref))
        wrong = sorted(a for a in ref.keys() & entries.keys() if ref[a] != entries[a])
        raise ValueError(f'{name}: mismatch: missing={list(map(hex, missing[:12]))} '
                         f'extra={list(map(hex, extra[:12]))} wrong={list(map(hex, wrong[:12]))}')
    print(f'PASS: {name}: {len(entries)} unique EE writes; '
          f'{PAYLOAD_SIZE} byte known-good payload SHA256={PAYLOAD_SHA256}; '
          'both 1664 MiB edits and hooks verified')


def render(blob: bytes) -> str:
    # Preserve the *known-working* three group arrangement and patch=0/1 mode.
    l = [
        '// Dark Cloud 2 (USA) v2.00, SCUS_972.13, ELF CRC 1DF41F33',
        '// Known-good HDD feature restoration: native US installation and HDD-backed gameplay verified.',
        '// Group names intentionally match the already enabled test patch; 189 executable writes unchanged.',
        '// Requires disc with the game\'s MODULES\\{DEV9,ATAD,HDD,PFS}.IRX;1 and PS2 HDD.',
        '// Preserve 32 MiB EE and 2 MiB IOP RAM. Back up the HDD image.',
        '// Patch mode 0 is boot-only, mode 1 maintains original US title-menu gate override.',
        '// CAUTION: temporary EE loader slot at 01FE8000 is not yet lifetime-proven on hardware/other boot paths.',
        '',
        '[Dark Cloud 2 - Existing HDD Title Menu Gate]',
        'description=Restore the fifth title menu option; do not bypass HDD detection.',
        'patch=1,EE,0029F2EC,word,00000000',
        '',
        '[Dark Cloud 2 - EXPERIMENTAL HDD Driver Loader]',
        'description=Proven game-side DEV9/ATAD/HDD/PFS initialization and sceFsInit before original InitCDFile. Boot-only scratch code.',
        '// Original init jal 0015C374 -> tiny stub 0015C398 -> temporary EE slot 01FE8000.',
        '// Slot contains own code, driver paths and args; resident IRX modules live in IOP RAM.',
        '// Failure falls through to original InitCDFile (no unbounded retries).',
        '// Diagnostic header DC2HDDv1 @01FE82B8; read at startup return 0015C37C only.',
    ]
    for a, (_, v) in words(blob).items():
        l.append(f'patch=0,EE,{a:08X},word,{v:08X}')
    for a, v in HOOKS.items():
        l.append(f'patch=0,EE,{a:08X},word,{v:08X}')
    l.extend([
        '',
        '[Dark Cloud 2 - US HDD Installer 1664 MiB Capacity Test]',
        'description=12 rather than 11 additional APA 128-MiB subpartitions; free-space check 1536 -> 1664 MiB.',
        '// Ghidra verified: original at 0031C4BC = 2A42000B, at 0031BF40 = 2A220600.',
    ])
    for a, v in CAPACITY_IMMEDIATES.items():
        l.append(f'patch=0,EE,{a:08X},word,{v:08X}')
    l.extend(['', '// No changes to runtime asset loader, game partition ID, marker, or original game archives.', ''])
    return '\n'.join(l)


def build_source(source: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix='dc2-us-hdd-') as work:
        obj = Path(work) / 'loader.o'
        elf = Path(work) / 'loader.elf'
        binary = Path(work) / 'loader.bin'
        subprocess.run(['clang', '-target', 'mipsel-unknown-elf', '-march=mips3',
                        '-c', str(source), '-o', str(obj)], check=True)
        subprocess.run(['ld.lld', '-m', 'elf32ltsmip', '--entry=_start',
                        '-Ttext', f'0x{BASE:08X}', '-o', str(elf), str(obj)], check=True)
        subprocess.run(['llvm-objcopy', '--only-section=.text', '-O', 'binary',
                        str(elf), str(binary)], check=True)
        return binary.read_bytes()


def check_log(s: str, wanted: dict[int, tuple[int, int]]) -> None:
    # PCSX2's patch activation output: time + mode,EE,word,addr,value.
    match = re.search(r'Enabled patch: Dark Cloud 2 - Existing HDD Title Menu Gate', s)
    if not match or 'Enabled patch: Dark Cloud 2 - US HDD Installer 1664 MiB Capacity Test' not in s:
        raise ValueError('Log does not include both restored title/menu and capacity groups')
    active = re.findall(r'Enabled patch: ([^\r\n]+)', s)
    if not any('EXPERIMENTAL HDD Driver Loader' in x for x in active):
        raise ValueError('Log lacks active HDD driver loader')
    # Existing widescreen/cheat patches may coexist, but neither a wrong write
    # nor conflicting writes to any restoration address can be ignored.
    by_addr = {}
    for t, a, v in LOG_ENTRY.findall(s):
        address = int(a, 16)
        entry = (int(t), int(v, 16))
        if address in wanted and entry != wanted[address]:
            raise ValueError(f'Conflicting EE write in activation log at 0x{address:08X}: '
                             f'observed {entry!r}, expected {wanted[address]!r}')
        if address in by_addr and by_addr[address] != entry:
            if address in wanted:
                raise ValueError(f'Conflicting duplicate active EE write at 0x{address:08X}')
        by_addr[address] = entry
    missing = sorted(a for a in wanted if a not in by_addr)
    wrong = sorted(a for a in wanted if a in by_addr and by_addr[a] != wanted[a])
    if missing or wrong:
        raise ValueError(f'Log differs from verified known-good: missing={list(map(hex, missing[:8]))} '
                         f'wrong={list(map(hex, wrong[:8]))}')
    print(f'PASS: PCSX2 log activates all {len(wanted)} verified patch entries '
          'including v1 header and both 1664 MiB capacity changes')


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', action='store_true', help='Rebuild from MIPS .S, verify SHA, write new .pnach')
    p.add_argument('--source', type=Path, default=DEFAULT_SOURCE, help='Known-good v1 loader MIPS source')
    p.add_argument('--output', type=Path, default=DEFAULT_PATCH, help='Unique .pnach output for --build')
    p.add_argument('--audit', type=Path, help='Statically validate an existing .pnach against known-good payload')
    p.add_argument('--verify-log', type=Path, help='Check PCSX2 activated patches against the verified payload')
    a = p.parse_args()
    if not a.build and not a.audit and not a.verify_log:
        p.error('Specify --build, --audit FILE and/or --verify-log FILE')
    # Source recompilation is always performed for a fresh build. For auditing
    # without clang, reconstruct the known-good payload from supplied .pnach.
    if a.build:
        blob = build_source(a.source)
        ref = expected(blob)
        result = render(blob)
        audit_text(result, blob, 'new release candidate')
        if a.output.exists():
            raise FileExistsError(f'Never overwrite an existing artifact: {a.output}')
        a.output.write_text(result, encoding='utf-8', newline='\n')
        print(f'CREATED: {a.output} ({len(ref)} entries)')
    else:
        if a.audit:
            candidate = parse_pnach(a.audit.read_text(encoding='utf-8-sig'))
            segment = [candidate.get(BASE+i, (None, None))[1] for i in range(0, PAYLOAD_SIZE, 4)]
        elif a.verify_log:
            candidates = {int(addr, 16): (int(t), int(v, 16))
                          for t, addr, v in LOG_ENTRY.findall(a.verify_log.read_text(encoding='utf-8-sig', errors='replace'))}
            segment = [candidates.get(BASE+i, (None, None))[1] for i in range(0, PAYLOAD_SIZE, 4)]
        else:
            segment = []
        if len(segment) != PAYLOAD_SIZE//4 or any(x is None for x in segment):
            raise ValueError('Cannot recover entire stage payload for validation')
        blob = b''.join(struct.pack('<I', x) for x in segment)
        ref = expected(blob)
    if a.audit:
        audit_text(a.audit.read_text(encoding='utf-8-sig'), blob, str(a.audit))
    if a.verify_log:
        check_log(a.verify_log.read_text(encoding='utf-8-sig', errors='replace'), ref)

if __name__ == '__main__':
    main()
