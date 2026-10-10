# Dark Cloud 2 US HDD restoration: known-good patch

**Target:** *Dark Cloud 2* USA v2.00, `SCUS_972.13`, executable CRC `1DF41F33`. The patch acts on game EE RAM; it neither modifies PCSX2 nor contains game assets or Sony system files.

## Status and boundaries

The actual US installer completed with the modified 1664 MiB PFS allocation; its partition was measured as 1553 MiB used, 111 MiB free. A cold game boot loaded DEV9/ATAD/HDD/PFS, detected `hdd` by module name, entered gameplay, and continued HDD reads with visibly faster loading. This is **operationally confirmed in PCSX2**, not proven on physical PS2 hardware or every emulator configuration.

The working loader resides temporarily at EE physical address `0x01FE8000` (736 bytes), after a short `0x0015C398` trampoline from the original `0x0015C374` call site. Its lifetime is *not* proved safe against all heap/stack layouts or soft resets. It initializes each IOP driver once, calls `sceFsInit`, and always calls the original `InitCDFile`. The game retains normal retail 32 MiB EE and 2 MiB IOP RAM visibility. **Do not substitute a different scratch address or alter IRX startup parameters without a full boot and gameplay regression.**

## Contents

- `DC2_US_HDD_Restoration_Clean_20261010_v4.pnach`: 189 EE word writes, **byte-for-byte equivalent to the validated v3 `.pnach`**. Only explanatory comments differ. Original three group names remain untouched so existing PCSX2 group toggles are not accidentally changed.
- `DC2_US_HDD_Loader_KnownGood_20261010_v4.S`: MIPS source, changes limited to comments. Rebuild output SHA-256: `b8f2bf8a1a974c6bcc505ac9fbaafbc9f396a665628c47925354c80af4861ef1` (736 bytes).
- `DC2_US_HDD_Restoration_Packager_20261010_v4.py`: standalone fail-closed packager. Checks every directive instead of silently ignoring malformed `.pnach` commands, verifies the exact payload SHA-256, all hook/capacity writes and per-command patch mode, refuses overwrite on fresh builds, and can compare PCSX2 patch-activation logs.
- `DC2_US_HDD_Restoration_Tests_20261010_v4.py`: standard-library `unittest` regression suite, including deliberately corrupted/duplicate/missing patch directives.

## Installation / migration

Place **one** active restoration `.pnach` for the exact CRC under PCSX2's patches directory (rename a copy to `1DF41F33.pnach` where appropriate). Disable/remove other copies of this restoration, including the earlier standalone driver loader or capacity test, rather than enabling duplicate writes. Preserve any unrelated patches only if their EE writes do not conflict. Do **not** delete or reinstall a working HDD partition for this update. Back up your PS2 HDD image before changing patches or testing new flows.

**Important:** The group titles still include `EXPERIMENTAL` / `Capacity Test` for compatibility with PCSX2's existing saved enabled-group settings. They are historical labels, not a claim that installation or normal HDD-backed gameplay remain untested. Enable all three groups. On a proper cold boot, the game-side IRX driver initialization should run once. Disable unsafe `Fast CDVD` if diagnosing disc-read timing.

## Verification

Run these commands from the directory containing the four files:

```powershell
python .\DC2_US_HDD_Restoration_Packager_20261010_v4.py --audit .\DC2_US_HDD_Restoration_Clean_20261010_v4.pnach
python .\DC2_US_HDD_Restoration_Packager_20261010_v4.py --verify-log .\pcsx2-boot-log.txt
python .\DC2_US_HDD_Restoration_Packager_20261010_v4.py --build --output .\DC2_US_HDD_Restoration_Rebuilt_20261010_v5.pnach
python -m unittest -v .\DC2_US_HDD_Restoration_Tests_20261010_v4.py
```

The `--build` path requires Clang MIPS target, `ld.lld`, and `llvm-objcopy`. Pure audit/tests require only Python 3.10+. Build refuses to overwrite the output; choose a new suffix per run.

**Executable changes:** `0x0015C374` and `0x0015C398` trampoline; `0x0029F2EC` menu visibility without bypassing HDD detection; `0x0031C4BC` `0x2A42000B` -> `0x2A42000C` (12 APA additions); `0x0031BF40` `0x2A220600` -> `0x2A220680` (1664 MiB preflight).

## Still requiring runtime validation

Before a universal release, check consecutive cold boots, a soft reset/return to title, disc fallback when no HDD is available, loading saves, and installation/uninstallation on a **disposable backup** image. Consider a memory-access/allocator watchpoint audit while the temporary EE loader runs. The v4 package does **not** change the underlying runtime loader or attempt a speculative safety fix.

OSD title/icon localization is explicitly out of scope for this cleanup.
