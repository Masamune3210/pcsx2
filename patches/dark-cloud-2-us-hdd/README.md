# Dark Cloud 2 (USA) native HDD support

**Verified runtime target:** `SCUS_972.13`, USA disc v2.00, ELF CRC `1DF41F33`. **Candidate target awaiting instruction-byte audit and boot verification:** USA disc v1.00 (Ghidra-imported executable hash metadata matches v2.00). This is a reversible PCSX2 `.pnach` game patch. It does not include copyrighted game assets or change the PCSX2 emulator.

## Disc revision compatibility (2026-10-11)

The newly supplied Ghidra export imported from `Dark Cloud 2 (USA) (v1.00)/SCUS_972.13` carries original executable SHA-256 `ac784f749c046cc78ebc3d7a026342a505354c422ffc55187b8128983d7d74e7` and MD5 `2d1aca0b2522a520a4e8839e88b7db69`. The preexisting US v2.00 Ghidra export records **both exact same hashes**, and both use MIPS R5900, image base `0x00100000`. This strongly suggests that the two imported ELFs represent identical original executables and that the **existing 127-write v5 `.pnach` may be reusable without address changes**. **This is not yet a verified US v1.00 compatibility result:** the six original instruction words have not been independently read from its Ghidra database, and the v1.00 disc has not been booted. Do not create a duplicate version-specific loader from inferred offsets. The existing patch targets CRC `1DF41F33`, which must also be confirmed during a v1.00 runtime boot.

**Required check before recommending the patch for US v1.00:** run `DC2_US100_HDD_Compatibility_Audit_20261011_v3.java` in the v1.00 Ghidra program and confirm **PASS** for all six original words; the script has been authored and committed but **has not been executed**. Then confirm the actual disc boots with the expected CRC and module resources before labeling v1.00 operational. **Evidence limits:** original-executable hashes are preserved Ghidra import metadata, not a fresh hash computed from re-ripped raw disc files. The two disc revisions may still differ in other files or in their bundled IRX modules; loading and installing from the *actual US v1.00 disc* remains to be tested. Never interpret matching ELF SHA-256 as proof all disc payloads are identical or the 1.00 run is tested.

## Stable PCSX2 restoration (v5)

**`DC2_US_HDD_Production_20261010_v5.pnach`** is the stable, cleaned-up restoration for the tested PCSX2 setup. It contains one **Dark Cloud 2 - HDD Support** group, 127 word writes, and a 488-byte EE boot loader. Four game-shipped IRX modules are started in order: DEV9, ATAD, HDD, PFS, followed by `sceFsInit`; the original `InitCDFile` runs afterward whether module startup succeeded or failed. The fifth title menu entry is restored without forging HDD detection, and the installer capacity is raised to 1,664 MiB using two verified instruction edits.

The previous verified experimental patch was 736 bytes / 189 EE writes. The 248-byte reduction eliminates per-module status storage, failure and stage counters, the completion flag, and the `DC2HDDv1` diagnostic marker. The v5 production patch **changes loader instructions and data layout** relative to v4. Its static rebuild, control-flow/API/argument checks and regression tests passed. **Extended PCSX2 gameplay validation is now reported by the user:** PCSX2 logged `Add 4855 seconds play time to SCUS-97213 -> now 35768` after using the cleaned v5 patch, with no reported crash or loading malfunction during that session. This is sufficient to classify v5 as **stable in the user's tested PCSX2 configuration**, not as universally validated on physical hardware or every reset path. Keep an HDD image backup; v4 remains available in Git history.

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

The v4 known-good files remain retrievable from the **`70948ffc24d9258888a73bc69cd3c42bd3983f7e`** Git commit; they are intentionally not part of the current patch directory. That tested loader completed the US native installation and HDD-backed gameplay with the two capacity edits. **v5 has now been exercised in extended PCSX2 gameplay without a reported failure**, and supersedes v4 as the recommended restoration. The original v4 remains recoverable for regression comparisons.

Research findings and verified evidence are recorded in `docs/research/dark-cloud-2-us-hdd-restoration.md`. HDD OSD title/icon localization is explicitly out of scope.
