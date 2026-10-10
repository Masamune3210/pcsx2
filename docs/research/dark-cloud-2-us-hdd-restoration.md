# Dark Cloud 2 (US) native HDD installation restoration

Status: **research parked; no patch implemented or tested**  
Repository branch: `research/dark-cloud-2-us-hdd-restoration` (from `master`)  
Baseline: *Dark Cloud 2*, USA v2.00, `SCUS_972.13`, PCSX2 2.9.115, game CRC `1DF41F33`, ELF 4,014,996 bytes.

## Why this branch exists

Investigate whether the native HDD installation/loading functionality available in Japanese *Dark Chronicle* can be made accessible in the US release. The US executable's Ghidra database retains HDD installation functions and apparent title-screen UI handling, despite no normally visible US install option. **This is game reverse-engineering research, not yet evidence of a PCSX2 defect.** Do not alter emulator behavior based on these observations alone.

## Observations so far

- The US and JP discs both ship `DEV9.IRX`, `ATAD.IRX`, `HDD.IRX`, **`PFS.IRX`**, along with network/audio/controller modules. The two supplied `MODULES` directory listings have identical file names, sizes and timestamps (24 files). **Byte-for-byte equality has not been verified**; hash the files.
- A decompilation of US `HDDMenuLoop__Fv` shows an internal *debug-font* interface with uninstall, install, and switch-CD/HDD-file-source actions, including installation thread progress, pause, cancellation, and cleanup. It calls `UninstallApp`, `CreateInstallThread`, `StepInstallThread`, `ChangeHddFile`, `ChangeDefaultFile`, and HDD state checks. It does **not** itself prove all downstream paths work.
- The US Ghidra symbol/reference database also identifies a separate title-screen installation UI: `TitleHDDInstallInit`, `TitleHDDInstallKey`, `TitleHDDInstallDraw`, and `CheckAppInstallForTitle`. Stored references suggest integration with `TitleInit`, `TitleLoop` and `TitleDraw`; inspect decompilation and concrete branch conditions before treating these as verified runtime call paths.
- `TitleHDDCheckFlag` is referenced from title initialization and mode input/drawing. Its exact semantics, assignments, and any region gating **remain unknown**.
- `ChangeHddFile` and filesystem/asset-loading symbols exist. Verify the implementations and whether ordinary loads switch from `cdrom0:` to `pfs0:`; an installer alone is not sufficient.

### Selected US Ghidra addresses (verified by v3 live Ghidra decompilation)

| Address | Symbol |
| --- | --- |
| `0029F1F0` | `TitleInit` |
| `0029FFA0` | `TitleLoop` |
| `002A1220` | `TitleModeKey` |
| `002A1B60` | `TitleModeDraw` |
| `002A3630` | `TitleHDDInstallInit` |
| `002A3A80` | `TitleHDDInstallKey` |
| `002A48F0` | `TitleHDDInstallDraw` |
| `002A4D00` | `CheckAppInstallForTitle` |
| `0031B110` | `HDDMenuLoop` |
| `0031BD50` | `MountHDDFileSystem` |
| `0031BFF0` | `CreateInstallThread` |
| `0031C270` | `InstallLoop` |
| `00148650` | `GetMainFileDev` |
| `00148660` | `ChangeHddFile` |
| `00376950` | `TitleHDDCheckFlag` (global, byte-sized) |
| `00376518` | `DefaultFileDev` (global) |

**Correction:** The prior `.gzf` address extraction omitted the ELF image base `0x100000`. All earlier mapping entries were `0x100000` too low. The v3 Ghidra diagnostics resolved names at the true addresses and decompiled functions successfully.


## Unmodified US PCSX2 boot baseline

Source: complete PCSX2 boot log from the investigation, `Pasted text (3).txt` (3,263 lines), starting at BIOS boot with HDD-enabled Browser 2.00 installed.

- PCSX2 host DEV9 exposes a 40 GiB HDD image. Early BIOS HDD loader successfully reads `pfs0:/osd100/hosdsys.elf`; Browser 2.00 later initializes its own DEV9/ATAD/HDD/PFS modules. **These HDD reads and module loads are from system software, not Dark Cloud 2.**
- The system software reboots/replaces the IOP environment multiple times during disc handoff. Do not assume a module loaded under Browser 2.00 remains registered when the game starts.
- At approximately `99.6358` through `99.9017` in this capture, *Dark Cloud 2* loads `SIO2MAN`, `PADMAN`, `MCMAN`, `MCSERV`, `LIBSD`, `SDRDRV`, `MODMIDI`, `MODHSYN`, `EZMIDI`, `EZBGM` from its disc. No game-side DEV9/ATAD/HDD/PFS module loads appear in the observed startup.
- An earlier partial gameplay log showed `searchbyname: module name hdd` and `return -202`. This establishes a failed module lookup at that point, **not** that the game lacks HDD code or necessarily initiated the lookup itself.
- Fast CDVD was enabled and a widescreen memory patch was present in the captured emulator configuration. Disable timing-altering options for controlled tests, and record any active patches.

## Japanese reference imported into Ghidra (2026-10-10)

The Japanese main executable `SCPS_150.33` has now been analyzed in **Ghidra 12.1.3** using `r5900:LE:32:default (1.4)`, image base `0x100000`. The Japanese `.gzf` archive has now been supplied; subsequently, the v3 live Ghidra reports verified actual addresses and yielded decompiled functions. See the later cross-region section for the resolved US language-dependent HDD menu gate.

- Japanese ELF MD5: `483996783678ccf04ead7eeff6c51e66`
- Japanese ELF SHA-256: `05985e62f09e9a2fb6d818323f422932b78c6dc1bc41d3757f18cade7451aa06`
- Japanese ELF comment: `MW MIPS C Compiler (2.4.1.01)`, `PlayStation2` (not verified against the US ELF comments)
- JP analysis: **7,748 functions**, **540,730 instructions**, **32,419 symbols**, **39,740 defined data items**, **249 datatypes**, **114,056,425 indexed bytes**.
- Earlier US analysis: **7,809 functions**, **548,645 instructions**, **32,768 symbols**, **44,691 defined data items**, **251 datatypes**, **114,173,295 indexed bytes**.
- Differences are analysis counts, **not proof of removed or added code**. Both programs use the same Ghidra processor language and original image base, easing decompilation/structural matching.

## Japanese vs US HDD function comparison (verified by v3 Ghidra decompilation, 2026-10-10)

V3 successfully resolved **51 of 52 targeted names in Japan**, and **all 52 in the US**. The sole missing JP name was `TitleHDDCheckFlag` (which is not necessarily absent as code or data). All earlier expected addresses were off by exactly `0x100000`, due to the earlier archive parse omitting the image base. The two 3,000+-line v3 reports contain successful decompilation of 27 functions per region. The previous v2 scripts correctly made **no** changes.

| JP real address | US real address | Symbol |
| --- | --- | --- |
| `00299F70` | `0029F1F0` | `TitleInit` |
| `0029ACD0` | `0029FFA0` | `TitleLoop` |
| `0029BF60` | `002A1220` | `TitleModeKey` |
| `0029C8B0` | `002A1B60` | `TitleModeDraw` |
| `0029E260` | `002A3630` | `TitleHDDInstallInit` |
| `0029E6E0` | `002A3A80` | `TitleHDDInstallKey` |
| `0029F550` | `002A48F0` | `TitleHDDInstallDraw` |
| `0029F910` | `002A4D00` | `CheckAppInstallForTitle` |
| `0029F950` | `002A4D40` | `CheckHDDInstall` |
| `00314AD0` | `0031B0C0` | `InitHDDMenu` |
| `00314B20` | `0031B110` | `HDDMenuLoop` |
| `00315760` | `0031BD50` | `MountHDDFileSystem` |
| `00315A00` | `0031BFF0` | `CreateInstallThread` |
| `00315C80` | `0031C270` | `InstallLoop` |
| `01EEF810` | `01F062D0` | `HDDINFO` |

The contiguous `InitHDDMenu` through `InstallLoop` functions maintain a constant US-minus-JP address shift of `+0x65F0`; the title-screen area shifts differently. Never apply the old +0x100000-less mapping to Ghidra or a patch.

### Key confirmed behavioral difference: US menu gated by language

- **JP `TitleInit`**: Initializes `HDDINFO` with `HddConectCheck` and calls `CheckAppInstallForTitle` and `CheckInstallSpace` with no `TitleHDDCheckFlag` guard.
- **US `TitleInit`**: `TitleHDDCheckFlag = LanguageCode < 1;`. HDD initialization checks later in `TitleInit` are guarded by `if (TitleHDDCheckFlag != 0)`.
- **JP `TitleModeKey`**: clamps title menu selection to 0..4. **US `TitleModeKey`**: clamps to 0..3 if flag is zero, otherwise 0..4. The HDD selection (`sVar1 == 4`) is otherwise present and checks `HDDINFO`.
- **JP `TitleModeDraw`**: draws all five options. **US `TitleModeDraw`**: draws four when flag zero and five when nonzero. Both dispatch into the HDD menu state in `TitleLoop`.
- The US `TitleHDDInstallInit/Key/Draw` code and installation state machine are largely parallel to JP. In the v3 decompilation, `CheckAppInstall`, `CheckInstallSpace`, `MountHDDFileSystem` are textually identical between builds; `InstallLoop` differs at three relocated data addresses. This is strong evidence of preserved installation code, **not runtime verification**.
- The US drawing code uses a `LanguageCode`-indexed `infomsg_2685` progress-message table, indicating locale-sensitive display logic.
- The storage-space calculation compares with `0x600` (1536 in the code's displayed MiB units), consistent with the ~1.5 GiB install requirement.

### Outstanding obstacle: IOP HDD drivers

`HddConectCheck` checks `sceSifSearchModuleByName` and subsequently uses `sceDevctl`, but does **not** itself load `HDD.IRX`. The existing unmodified US boot log does not show the game's DEV9/ATAD/HDD/PFS loading during startup. The earlier `hdd` module lookup failure (`-202`) is consistent with this. Need trace the actual game-side driver loading path in JP (and its region restriction if any) before asserting that changing the flag is sufficient.

### Patch preparation, not yet patched

Real US Ghidra global: `0x00376950 TitleHDDCheckFlag` (byte). The US v3 report gives incoming references at `0x0029F2E8`, `0x0029F2EC` (writes), `0x0029FE8C` (title detection), `0x002A16D8` (menu selection), and `0x002A1DD0` (menu rendering). **No instruction bytes have yet been verified**, so do not blindly patch these addresses. Run the new read-only `DC2HddGateInstructionAuditV4.java` to export listing mnemonics/raw instruction bytes and inspect before building a reversible patch. Work on a copy of the HDD image.

Metadata applicable to Ghidra:
- US `0x00376950`: existing label `TitleHDDCheckFlag`, one-byte flag (do not overwrite a valid existing declaration).
- US `0x01F062D4` -> proposed label `TitleHddDeviceState`, 32-bit int written through `HddConectCheck`.
- US `0x01F062D8` -> proposed label `TitleAppInstallState`, 32-bit int from `CheckAppInstallForTitle`.
- US `0x01F062DC` -> proposed label `TitleInstallSpaceState`, 32-bit int from `CheckInstallSpace`.
- JP `0x01EEF814`, `0x01EEF818`, `0x01EEF81C` correspond to the three above, in order. Check existing data/symbol overlap before relabeling.
- Corrected full address list: `DC2_JP_US_HDD_Verified_Ghidra_Addresses_20261010_v4.tsv` (local).

### Current next step

Obtain the v4 instruction listing for US flag writer and HDD-check call sequence, then prepare a minimal, reversible **game patch** to test menu exposure. Independently determine how JP loads the HDD stack, then test install/mount/file-source behavior under PCSX2. The findings concern the game executable; no PCSX2 emulator changes have been made.

## V4 instruction listing and reversible menu-exposure test (2026-10-10)

Both supplied live-Ghidra instruction audits completed with **0 unresolved states** and made no DB or executable changes:
- `DC2_US_HDD_Gate_Instruction_Audit_20261010_v4.txt`
- `DC2_JP_HDD_Gate_Instruction_Audit_20261010_v4.txt`

**Confirmed US code at the real EE execution address**:

```text
0x0029F2DC  lw    v0,-0x7530(gp)     ; LanguageCode
0x0029F2E0  li    v1,1
0x0029F2E4  blez  v0,0x0029F2F0
0x0029F2E8  _sb   v1,-0x7ba0(gp)     ; delay slot always sets TitleHDDCheckFlag=1
0x0029F2EC  sb    zero,-0x7ba0(gp)   ; clears flag only for positive LanguageCode
```

Original little-endian bytes at `0x0029F2EC`: `60 84 80 A3`, instruction word `A3808460`. A **single 32-bit NOP** (`00000000`) at that address should leave the flag enabled for every language while preserving other `LanguageCode` effects. Experimental PCSX2 `.pnach` line for **SCUS_972.13 US v2.00 / CRC 1DF41F33**:

```ini
patch=1,EE,0029F2EC,word,00000000
```

**2026-10-10 loader correction:** The initial file `DC2_US_HDD_Menu_Exposure_20261010_v1.pnach` contained an **unlabelled** `patch=` line, which is not shown as a selectable GUI patch. In this fork's `pcsx2/Patch.cpp`, `PatchStringHasUnlabelledPatch()` also causes `EnumeratePnachFiles()` to **suppress bundled `resources/patches.zip` entries** for that game to prevent conflicts. This matches the user's observation that adding the test file made existing bundled game patches disappear. The file name should follow `<Serial>_<CRC>.pnach`. This is distinct from compatibility patch lines in `GameIndex.yaml`.

**Corrected local replacement artifact (not committed to repo): `SCUS-97213_1DF41F33.pnach`**. Remove the older unlabeled file from the `patches` directory rather than keeping both. Contents:

```ini
[Dark Cloud 2 - Native HDD Menu (Experimental)]
description=Expose the fifth title-menu entry in the US v2.00 release. Does not load HDD IOP drivers; use a backup HDD image.
patch=1,EE,0029F2EC,word,00000000
```

The section label makes the patch individually toggleable in Game Properties > Patches and allows `EnumeratePnachFiles()` to load bundled ZIP patches as well. PCSX2 must rescan/reload the game after replacement. **Not yet runtime-tested.** The `cheats` directory is a valid alternative but needs cheats enabled and appears in the Cheats UI. No emulator or ELF change has been committed.

**Expected effect:** US `TitleInit` performs the title HDD detection block; `TitleModeKey` accepts selection index 4 instead of clamping to 3; `TitleModeDraw` draws five instead of four options. This is the complete identified menu gate. The JP title makes the HDD check unconditionally (`jal HddConectCheck` at `0x0029ABD0`) and always permits five entries.

**Important independent blocker:** `TitleModeKey` at US `0x002A17F0` loads `HDDINFO` and refuses entry into the installer if `HDDINFO <= 0`; it is stored at `0x01F062D0` (heap, no initialized bytes available in Ghidra). `HddConectCheck` calls `sceSifSearchModuleByName("hdd")` and returns zero if the IOP module is absent. The unmodified US boot log already showed a failed `hdd` module lookup. **Do not force `HDDINFO=1`**: that would bypass the actual driver check and risks unsafe HDD operations. Determine JP and US game-side module-load sequence separately.

Additional local read-only script prepared: `DC2HddIopModuleLoadAuditV5.java` (not yet executed inside Ghidra). Run in JP and US to scan initialized sections for IRX/IOPRP/HDD/PFS strings, enumerate module-load/IOP-reset function references, and decompile their candidate callers. It prints an unresolved-states summary at the end. This targets the independent driver-loading blocker without changing any executable or Ghidra metadata.\n\nNext test: on a **copy of the HDD image**, apply only the `.pnach` menu-gate line, verify whether the fifth title item draws, capture game-side IOP module loads/module lookups, and report whether the fifth item is enabled. If still greyed out, trace JP `DEV9/ATAD/HDD/PFS` initialization and equivalent US functions. An exposed item alone is not proof of a functional installer.


## Runtime test: US install menu exposed but disabled (2026-10-10)

User screenshot of *Dark Cloud 2* US in PCSX2 after applying corrected, labelled `.pnach` shows **five title menu entries**: New Game, Continue, Extras, Options, and **Install** (English). The cursor is positioned beside Install, but its text appears greyed out. This is **positive visual verification** that the `0x0029F2EC` NOP successfully exposes and draws the fifth menu entry using surviving English assets. It is **not evidence of HDD detection success or installer execution**.

The reported disabled appearance agrees with the decompiled `TitleModeDraw` grey-out condition `index == 4 && HDDINFO == 0`, and `TitleModeKey` refuses to enter when `HDDINFO < 1`. Prioritize obtaining the actual return value from `HddConectCheck`, then trace why the game's IOP does not have a suitable `hdd` module when queried, comparing JP driver setup and US setup. Do **not** force `HDDINFO > 0` as a shortcut. The screenshot alone does not establish precise runtime register values or distinguish missing module from other detection failures.

## BSim propagation baseline (2026-10-10)

BSim propagation was run **between the Japanese and US Ghidra databases in both directions**. **No additional labels were created in either database**, per the observed result. Match counts, confidence/similarity thresholds, ignored/conflicting matches, and the exact set of correspondences have not yet been exported or verified. Do not infer that no functions matched or that the binaries are structurally identical: both databases already contain extensive Metrowerks-mangled function symbols. Next targeted investigation remains the decompiled title/HDD conditional checks rather than another broad propagation pass.


## Historical v2 audit failure (resolved by v3)

V2 required exact primary symbol at addresses extracted incorrectly without the image base and thus reported zero matches. V3 searched by name, recovered the real locations (`+0x100000`), and exported 27 function decompilations per region. Both versions were read-only. Use corrected v4 address list and instruction audit; do not rerun v2.

## Resume plan (smallest useful next steps)

1. **Completed:** v3 diagnostic exported 27 decompiled functions per region. Next: run the read-only v4 instruction-byte audit at the verified US title HDD flag references.
2. **Completed at decompiler level:** the US gate is `TitleHDDCheckFlag = LanguageCode < 1`, read during initial HDD checks, title selection and rendering. Still verify exact raw machine instructions before patching.
3. **Partially complete:** these functions exist and largely match Japanese decompiled logic. Now resolve game-side DEV9/ATAD/HDD/PFS driver-load path and ensure English installer resources are present and usable.
4. Compare the **Japanese and US main ELF machine code and branches**, associated menu assets, file layouts, and module hashes. Symbol-address matching is complete, but implementation parity is unverified. Confirm any differences that matter to the installer and HDD-backed loading.
5. If supported by analysis, make the **smallest reversible game-side patch** on a separate experimental artifact and check for new *game-side* HDD IRX loads and PFS activity. A visible menu is only a first milestone; validate installation, loading and fallback independently.

### Safety / verification

- Work on **copies** of the PCSX2 HDD image and disc/ELF; both `UninstallApp` and installation code may mutate partitions.
- Avoid committing proprietary PS2 executable, IRX files, HDD image or Ghidra database to this public repository. Keep exported analysis reports locally.
- Don't conflate host DEV9 initialization, BIOS/OSD module loads and the game's own IOP environment.
- No code changes or test results are claimed on this branch as of this handoff.

### Local research artifacts from the original investigation

- `SCUS_972.13.gzf`: US Ghidra program archive.
- `DC2_US_HDD_Symbol_Reference_Map_20261010_v1.tsv`: exported stored-symbol/ref address map.
- `DC2_US_HDD_Decompile_Audit_20261010_v1.py`: Ghidra Jython read-only audit exporter.
- `Pasted text (3).txt`: complete stock-US PCSX2 boot log.
- `Pasted text (2).txt`: shorter boot/gameplay log containing HDD module lookup.
