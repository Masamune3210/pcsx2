# Dark Cloud 2 (USA) native HDD support

**Target:** `SCUS_972.13`, USA v2.00, CRC `1DF41F33` (original disc). This is a reversible PCSX2 `.pnach` game patch. It does not include copyrighted game assets or change the PCSX2 emulator.

## Production candidate

**`DC2_US_HDD_Production_20261010_v5.pnach`** is the clean patch to test. It contains one **Dark Cloud 2 - HDD Support** group, 127 word writes, and a 488-byte EE boot loader. Four game-shipped IRX modules are started in order: DEV9, ATAD, HDD, PFS, followed by `sceFsInit`; the original `InitCDFile` runs afterward whether module startup succeeded or failed. The fifth title menu entry is restored without forging HDD detection, and the installer capacity is raised to 1,664 MiB using two verified instruction edits.

The previous verified experimental patch was 736 bytes / 189 EE writes. The 248-byte reduction eliminates per-module status storage, failure and stage counters, the completion flag, and the `DC2HDDv1` diagnostic marker. The production candidate **changes loader instructions and data layout**. It passes static rebuilding, control-flow/API/argument checks and regression tests, **but is not yet runtime verified**. Do not discard your verified v4 patch or HDD backup until v5 passes cold boot and gameplay.

**Unchanged critical entry points:**
- Title gate `0x0029F2EC`: `00000000` (patch mode 1).
- Original init call at `0x0015C374`: `0C0570E6` -> padding trampoline `0x0015C398` (`087FA000`) -> loader at `0x01FE8000` (mode 0).
- Installer loop at `0x0031C4BC`: `2A42000C` (12 subpartitions).
- Installation space check at `0x0031BF40`: `2A220680` (1664 MiB).

**Important:** `0x01FE8000` is still a temporary EE address not proven universally safe over arbitrary heap layouts, resets, or physical PS2 hardware. The release must not claim this lifetime issue is solved by removing diagnostics. The tested setup preserves retail 32 MiB EE / 2 MiB IOP memory.

## Using it

Make a backup of the PCSX2 HDD image. Replace the old three-group HDD restoration with **only** the new production `.pnach` contents (do not append both loaders). The new single-group name is intentional and may require enabling **Dark Cloud 2 - HDD Support** in PCSX2's patch controls after the switch. Existing unrelated widescreen or cheat groups may remain separately enabled.

**The successful 1664 MiB US installation does not require reinstalling**: first test an existing save and existing installed game data. Verify a *cold boot* (not save-state reload), title menu, startup of the four HDD drivers, correct HDD detection, actual gameplay loading, return to title, repeated launch and no-HDD fallback. Retain the v4 patch if v5 fails any of these.

## Rebuild and audit

From the patch directory, on a system with Python 3, `clang`, `ld.lld` and `llvm-objcopy` installed:

```powershell
python .\DC2_US_HDD_Build_Production_20261010_v5.py --audit .\DC2_US_HDD_Production_20261010_v5.pnach
python -m unittest -v .\DC2_US_HDD_Production_Tests_20261010_v5.py
python .\DC2_US_HDD_Build_Production_20261010_v5.py --build --output .\DC2_US_HDD_Production_Rebuilt_20261010_v5.pnach
```

Build refuses to overwrite an existing output. The audit rejects unexpected instructions and modified startup code, malformed/unknown directives, duplicate addresses, wrong group arrangement, changed capacity instructions, or changed original hook. The 488-byte code is SHA-256 `bea8b5981c092fa501acee8884db834c1605b52fe0110a5b13250963efda48d4` and contains no diagnostic state table.

## Preservation and research

The v4 known-good files remain retrievable from the **`70948ffc24d9258888a73bc69cd3c42bd3983f7e`** Git commit; they are intentionally not part of the current patch directory. That tested loader completed the US native installation and HDD-backed gameplay with the two capacity edits. **This v5 file has not been boot-tested**, and the original v4 remains the fallback.

Research findings and verified evidence are recorded in `docs/research/dark-cloud-2-us-hdd-restoration.md`. HDD OSD title/icon localization is explicitly out of scope.
