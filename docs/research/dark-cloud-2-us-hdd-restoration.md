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


## V5 IOP driver loader comparison and ROMIMG possibility (2026-10-10)

The JP and US **read-only Ghidra v5 reports have completed with zero unresolved states**. The US install menu is now proven visible via the existing `.pnach`, but greyed out when `HDDINFO` indicates no driver connection.

JP `SCPS_150.33` contains `LoadHddModule__Fv` at **`0x0015BD00`**, which does the following, in strict order:

1. `sceSifLoadStartModule("cdrom0:\\MODULES\\DEV9.IRX;1", 0, NULL, &result)`.
2. Same for `ATAD.IRX`, with no start arguments.
3. `HDD.IRX`, with **0xB bytes of startup arguments** prepared in a local stack buffer.
4. `PFS.IRX`, with **0x11 bytes of startup arguments**, also built from compiled constants.
5. `PreparePowerOff()` and `sceFsInit()` on the successful path.

The module loader's calls loop on **negative loader return values**; then the `result == 1` check leads to error `-1`. This behavior and the exact argument bytes should be verified before porting. JP also has `PowerOffThread` (including `pfs:` and `hdd:` paths). The US v5 report has neither the four disc driver path strings nor `LoadHddModule` as a discovered symbol, and has **no game-side direct calls to `sceSifLoadStartModule`** other than the common wrapper. The regular US `init__Fv` ends after ten controller/memory-card/audio module loads and `InitCDFile`. `HddConectCheck` in both builds only checks for a resident module named `hdd`, then issues `sceDevctl("hdd:",0x4807,...)`; it does not load drivers.

**Caller now established from live JP Ghidra listing:** `LoadHddModule__Fv @ 0x0015BD00` has a direct call XREF from `init__Fv @ 0x0015BC4C`. This proves the loader is reachable from the game's startup *call graph*, but not whether a preceding branch conditions execution at runtime. The narrow next step is JP disassembly/decompilation surrounding **`0x0015BC4C`**, with US `init__Fv` around/after `0x0015C364` for comparison. V6 can still export caller context and raw constant bytes, but no longer needs to rediscover the callsite. The JP loader has deliberate register returns `0` on the completed path and `-1` on module startup failures, so its function datatype should be `int LoadHddModule__Fv(void)` (Ghidra function editor, no semicolon).

### JP loader assembly: call and driver startup options (2026-10-10)

The JP `LoadHddModule__Fv` listing at `0x0015BD00` confirms `sceSifLoadStartModule` calls at `0x0015BD58` (DEV9), `0x0015BD90` (ATAD), `0x0015BDC8` (HDD), `0x0015BE00` (PFS). The loader retries if its direct call result is negative and returns `-1` if the module's `result` output equals 1. On success it calls `PreparePowerOff @ 0x0015BE28` followed by `sceFsInit @ 0x0015BE30`, logs a negative FS initialization return but ultimately returns 0. JP disassembly supplies a direct `init__Fv:0x0015BC4C` inbound call reference. This *closes the symbol-reachability uncertainty* from v5 but does not prove unconditional execution.

The packed little-endian initialization constants appear to encode NUL-separated startup arguments:

- HDD (0x0B bytes): `-o\0 2\0 -n\0 20\0` (hex `2D 6F 00 32 00 2D 6E 00 32 30 00`).
- PFS (0x11 bytes): `-m\0 2\0 -o\0 16\0 -n\0 40\0` (hex `2D 6D 00 32 00 2D 6F 00 31 36 00 2D 6E 00 34 30 00`).

The displayed listing did not disclose the final byte values at `DAT_0032CD4A` / `DAT_0032CD60`; these NUL terminators are **strongly inferred** but should be checked in Ghidra's byte view or v6 export before calling the argument arrays byte-verified. This is another reason not to assume reboot-image preloading with defaults is equivalent to the JP post-reboot loader.

### ROMIMG alternative under consideration

The user suggested using **ROMIMG** to inject DEV9/ATAD/HDD/PFS into the disc's `MODULES/IOPRP243.IMG`. ROMIMG supports listing/extracting/adding files to IOPRP images (its `-l/-x/-a` commands); this is **plausible as a game-disc-side alternative** to transplanting the Japanese EE module-loading routine. But **merely copying IRX entries into ROMDIR will not autoload them**. The image boot configuration (`IOPBTCONF`) must actually request those modules, and module order/dependencies matter. The JP runtime loader supplies explicit 0xB/0x11 byte start arguments for HDD/PFS, which a simple list of module names in IOPBTCONF is not proven to replicate. `PreparePowerOff` and `sceFsInit` are also part of the successful Japanese flow. Test these independently; do not bypass `HDDINFO`.

### IOPRP ROMDIR comparison completed (2026-10-10)

User ran `romman-windows.exe -l` (PS2 ROM Manager, built 2025-11-22) on the stock Japanese `dc2jp/MODULES/IOPRP243.IMG` and US `dc2us/MODULES/IOPRP243.IMG`. **Both outputs are identical at the metadata/listing level:** 253,201-byte images, 16 entries, image metadata date 2001-12-05, comment `20011205-113144,conffile,ioprp243.img,xokano@rel-linux/~/tmp_243`, `has bootstrap: No`, zero gap/deadspace. Both list `RESET`, `LOADCORE`, `SIFCMD`, `SIFMAN`, `THREADMAN`, `IOMAN`, `MODLOAD`, `FILEIO`, `CDVDMAN`, `CDVDFSV`, `LOADFILE`, `TIMEMANI`, `ROMDRV`, `EESYNC`, `SYSCLIB`, `STDIO`. **No DEV9, ATAD, HDD, PFS, or IOPBTCONF entries** in either ROMDIR listing. Entry offsets, sizes, versions, dates match; cryptographic equality still unverified.

**Interpretation:** JP's known working native HDD installer does not require HDD drivers to be *contained in its stock reboot image*. It uses `LoadHddModule` to load the four modules from their separate disc files with explicit 0xB- and 0x11-byte arguments for HDD/PFS. Direct insertion with ROMIMG would grow a zero-free-space image and **would not itself establish module startup**. Do not infer that the ROMMAN `has bootstrap: No` flag by itself means the entire IOPRP cannot be booted; it is a ROM manager descriptor, not a boot trace.

**Priority:** Determine real incoming call sites/reachability for JP `0x0015BD00 LoadHddModule` using the prepared v6 audit and compare `init`, then decide whether to restore the EE loader or design a justified alternative. No IOPRP modifications have been made.

Before modifying an image: (a) hash-compare JP and US IOPRP images, (b) `ROMIMG -l` and `ROMIMG -x` **on copies** of both IOPRPs, (c) inspect embedded `IOPBTCONF` and module names and confirm correct entry order, (d) examine JP callsite/startup argument bytes, (e) use a disposable ISO/game image and backup virtual HDD for runtime tests. A patched IOPRP changes disc layout and must be rebuilt correctly. There is **no validated ROMIMG image or working HDD-install test** yet.

Technical references: https://sites.google.com/view/ysai187/home/projects/romimg ; https://psi-rockin.github.io/ps2tek/ (IOPBTCONF and ROMDIR formats). No emulator source changes requested or required on this track.


## Fully annotated JP HDD flow (Codex handoff, 2026-10-10)

Supplied updated `SCPS_150.33(2).gzf` and `2026-10-10-1252-dc2jp-hdd-saved-annotations.md`. The archive is a valid Ghidra Java-serialization stream with an embedded ZIP payload and the expected `Dc2Hdd_LoadModules`, `Dc2File_SwitchToHdd`, `Dc2Hdd_InstallerThread`, and datatype-category names were **independently found in its decompressed payload**. The report says the saved JP program has 153 annotated functions (147 fully contracted), 73 descriptive renames with original aliases retained, 56 typed globals, 15 structures, 10 enums, three callback definitions, 18 typed stack locals, two corrected local leaf conventions, and 43 newly disassembled instructions. All 153 decompilations reportedly passed Codex's fresh-save verification. This is Ghidra/database verification, **not a running HDD install or US code verification**.

### Newly established Japanese runtime contract

- Startup `0x0015B9E0 Dc2System_Initialize` calls `0x0015BD00 Dc2Hdd_LoadModules` as previously found; the corrected **signed integer return type** resolves a stale decompiler check. Loader calls DEV9, ATAD, HDD (11 argument bytes), PFS (17 bytes), prepares power-off, and initializes the EE filesystem.
- Install partition `PP.SCPS-15033.APP.DARKCHRO`: **one 128 MiB main partition + eleven 128 MiB subpartitions** (1536 MiB allocated). Format PFS with 8192-byte zone. **Check whether US source retains the Japanese ID or substitutes `SCUS` before porting.** Preserve JP partition identity during comparative tracing.
- Source archive is **`DATA.HD3`, not `DATA.HD2`**; the independently analyzed JP archive contains 6389 records, of which 6356 are eligible for copying (1,493,293,621 payload bytes). Large transfer arena `0x700000` bytes, with installer stack `0x19000` at aligned beginning.
- Install header file `etc/hdd_head.hed` incorporates `SYSTEM.CNF` according to header offsets. JP SYSTEM.CNF advertises `HDDUNITPOWER=NICHDD`.
- The install marker is `Ver0.208\0` (nine bytes), written **only after** the complete file iteration. Validation checks main partition and marker rather than independently checksumming copied files. Error codes `-1000` missing version, `-1001` mismatch. A mount-busy `-16` is accepted without unmounting another owner's mount; resource mounts are read-only.
- Pause **changes worker priority from 10 to 11**, not thread suspension; cancellation is polled between eligible completed files, so unbounded CD retry loops can delay cancellation. The worker does not consume deletion's separate `EndFlag`.
- Ordinary file loading supports CD/PFS backends through path/device selection. **Movies explicitly force disc access.** Fatal I/O callback loops permanently for `-5` or `-65541`, without falling back to disc. Background slots: 32 × `0x120`; cache slots: 16 × `0x40`.
- Separate priority-1 HDD shutdown worker closes PFS handles, retries HDD/DEV9 shutdown and CD power-off; ordinary main exit does **not** run that sequence. Do not omit power-button handling in an end-to-end implementation.

### Database and analysis caveats

The saved JP program repaired three function extents (installer UI `0029E6E0..0029F277`, lowercase loops `001492A0..001492EB` and `003152A0..003152EB`) and removed two false interior functions. It also restored an 11-phase jump table at `00365B20` by adding computed refs and a jump-table override at `0029EC58`. It preserved original bytes and instructions. The report notes preexisting AU-Sound data overlap at uncached `2036960F`, and an R5900 p-code unsigned DIVU issue, neither to be silently rewritten.

The source report's local verification outputs (`verify.c`, `verify-native.txt`, `verify-index.json`, datatype manifest, `module-hashes.json`, and `verified.json`) were referenced but **not uploaded in this turn**. Do not claim independent verification of those auxiliary outputs. The supplied JP archive contains saved annotations and updated Ghidra metadata; it is the starting point on resume, not the older JP program snapshot. Do **not** rerun the original Codex baseline/preview/apply process on the already annotated program.

### US baseline availability and preliminary direct comparison (2026-10-10)

The original US `SCUS_972.13.gzf` **already exists in conversation files** alongside the US v3 decompilation report, v4 gate instruction audit, and v5 IOP loader audit. **Do not ask for another US Ghidra export solely because the newest Japanese annotations were saved.** The minor US metadata application script (if run) does not alter original executable instructions, so the old US decompilation and loader reports remain a valid behavioral baseline. An attempted `files.materialize` call for the old US archive reported a staged path but the current container did not expose it; use the already present US reports for analysis until raw archive bytes are actually accessible, and do not assert byte-level verification from them.

A direct parser comparison of the 27 earlier JP/US v3 function decompilations showed **16 textually identical function bodies**: `TitleDraw`, `CheckAppInstallForTitle`, `CheckHDDInstall`, `InitHDDMenu`, `HddConectCheck`, `CheckAppInstall`, `MountHDDFileSystem`, `UmountHDDFileSystem`, `CheckInstallSpace`, `CreateInstallThread`, `StepInstallThread`, `GetInstallProgress`, `DeleteInstallThread`, `InstallPause`, `InstallCancel`, and `GetMainFileDev`. `InstallLoop` had near-identical textual output, with relocated data references. The JP Codex new type annotations require separate verification before applying any US ABI change. Decompiler-text identity alone is **not proof of byte-identity or runtime parity**.

### Direct 27-function JP-to-US comparison (2026-10-10)

Continued without requesting a redundant US export: both original region-specific v3 decompilation reports were parsed by symbol and compared. There are **27 verified paired functions, 16 exactly identical exported C bodies**, and 11 different: title-init/selection/drawing regional gates, title-loop/installer UI and localization/controller/data relocations, debug text addresses, near-identical installer worker with **three relocated data references**, and `TopDir` expression/declaration differences in the two device-switching helpers. Decompiler-text identity is not machine-code identity and does not validate the full JP Codex annotation transfer. The existing US archive is indexed in conversation files; no need to upload it again just to continue.

Prepared local standalone artifacts (verified present as files, *not executed in Ghidra*):

- `DC2_JP_US_HDD_Lifecycle_Comparison_20261010_v5.tsv`: full 27-function pairing, classification, and next-check notes;
- `DC2_JP_US_HDD_Restoration_Decisions_20261010_v5.md`: detailed comparator result, Ghidra-function-editor-compatible **partial US signature refinements** for functions whose exported C bodies are identical, and prioritized implementation blockers;
- `DC2_US_HDD_Startup_And_Installer_Audit_20261010_v2.java`: read-only focused Ghidra script for the **already-existing** US database. Checks `init @0x0015C160` tail around `0x0015C328..0x0015C3A8`, incoming references to module-loader APIs, `TitleHDDInstallKey @0x002A3A80` coverage across possible jump-table/body truncation, and reads US raw marker/asset bytes and source string symbol locations. Invalid states printed at bottom. **Not run or Ghidra-verified**.

Important open questions before proposing an ELF patch:
1. Where to insert/hook the missing US game-side DEV9/ATAD/HDD/PFS startup sequence **without overwriting valid code**; JP uses 11- and 17-byte arguments and filesystem/power-off initialization.
2. Actual US partition ID, version marker, and source archive/resource path bytes. Earlier US `InstallLoop` references raw address `0x00372E20` for nine-byte marker and `0x00372ED0` for a loaded header resource, but contents not yet exported. Do not claim they match JP without byte check.
3. Repair the US installer function extent/11-phase dispatch independently. JP Codex fixed `0x0029E6E0..0x0029F277`; US `0x002A3FF8` has computed dispatch, and old US report associates nearby references with no function owner.

**Audit script compile fix (2026-10-10):** Ghidra 12.1.3 rejected v2 because the `references`, `instructions`, and `constants` helpers call `monitor.checkCancelled()` without declaring checked `ghidra.util.exception.CancelledException`. New local output `DC2_US_HDD_Startup_And_Installer_Audit_20261010_v3.java` imports that type, declares `throws CancelledException` on all three helpers, and matches the public Java class name to the new basename. No logic or database behavior changed. **Not yet executed in Ghidra**. The follow-on “class not found” error was caused by the preceding compile failures.

No new PCSX2 code or US executable patch, beyond the already runtime-proven menu-only NOP, was made. Continue by reviewing targeted US audit output, not by another broad propagation or requesting existing archives.

## V3 US native startup and installer report: 2026-10-10

User ran the corrected `DC2_US_HDD_Startup_And_Installer_Audit_20261010_v3.java` on existing `SCUS_972.13`; report `2(2)` completed. **No executable or Ghidra modifications** in that audit.

- `init__Fv @ 0x0015C160` body max `0x0015C397`. `main @0x0015C3A0` calls it from `0x0015C3C0`.
- Last non-HDD module API call `sceSifLoadModule @ 0x00117D38` is `0x0015C364`; `0x0015C374` calls `InitCDFile @ 0x00148F70`; then final calls `0x001042E8`, `0x001027D0`, epilogue `0x0015C38C-0x0015C394`. Addresses `0x0015C398/0x0015C39C` are **eight bytes of zero padding before main**, not a disassembly defect. The API `sceSifLoadStartModule @0x00117D58` has only an EXTERNAL entry ref, no game-side caller.
- `HddConectCheck @ 0x0031BA40` has expected multiple callers, including `TitleInit @0x0029FE98`, `TitleHDDInstallInit @0x002A39A4`, `CheckHDDInstall @0x002A4D48`, mount/debug calls. It still does not load `hdd`.
- `TitleHDDInstallKey @0x002A3A80` currently has **noncontiguous function ownership**, although its recorded body max is `0x002A4617`. The 11-entry computed switch is at `0x002A3FF8` (native `jr v0` word `0x00400008`); its table base is **`0x0036E2D0`** (derived directly from US `lui v1,0x37; addiu v1,-0x1d30`, not inferred using a JP delta). Phase case instructions at `0x002A402C, 0x002A4160, 0x002A43FC, 0x002A4468` have no containing function; `0x002A4614` is within original function. Repair metadata, don't rewrite instructions or assume cases absent.
- Raw US nine-byte install version marker at `0x00372E20` is **`Ver0.309\\0`**, distinctly newer than the Japanese `Ver0.208\\0`. Raw resource path at `0x00372ED0` is `etc/hdd_head.hed\\0`; immediately afterward a string `cdrom0:\\SYSTEM.CNF;1` is stored. These are verified byte reads from US live Ghidra, not assumptions from JP.
- `TitleHDDCheckFlag` remains stock in the Ghidra database at `0x0029F2EC` (`60 84 80 A3`); the working one-NOP title patch was applied only at PCSX2 runtime, not to the original binary.
- Audit summary listed two undefined instruction addresses `0x0015C398`, `0x0015C39C`; both have raw bytes `00 00 00 00` and lie **outside the init function at its end**, before `main`. Classify as **expected padding**, not a repair target.

New local scripts prepared (not yet executed in Ghidra):

1. **`DC2_US_HDD_Installer_Switch_Repair_20261010_v1.java`** applies a guarded Ghidra-only function-body repair to `0x002A3A80..0x002A4617`, validates all eleven 32-bit targets from `0x0036E2D0` fall on existing instructions inside the range, rejects ownership conflicts, and adds computed jump xrefs from `0x002A3FF8`, plus three non-destructive labels and comments. Transaction rolls back on conflict; does not modify executable bytes or automatically force Ghidra function signatures. **May decline to apply if another spurious function overlaps or a target is undisassembled.**
2. **`DC2_US_HDD_Loader_Resources_And_CodeCaves_20261010_v1.java`** is a read-only bounded main-block string and zero-run scanner to find the actual US partition/archive constants and conservative unreferenced candidate locations for an EE-native loader. A code cave is *not* established as safe merely from static zero bytes; verify ELF file backing, loader map, actual execution and ownership before use.

Dependency order for patching: confirm US source archive/partition strings and resolve installer jump table/body; select a safe EE payload location or other implementation before creating game patch bytes. Do not expand the 8-byte gap at `0x0015C398..C39C` into a driver loader: it is far too small. Do not bypass `HDDINFO`. Continue from provided report instead of repeating the earlier audit or requesting the already-provided US Ghidra archive.


### Phase-4 Ghidra decode guard (2026-10-10)

The user ran `DC2_US_HDD_Installer_Switch_Repair_20261010_v1.java` on US Ghidra. Its exact 11 installed jump-table destinations were: phase 0 `002A45EC`, 1 `002A430C`, 2 `002A4000`, 3 `002A4114`, **4 `002A4334`**, 5 `002A43E0`, 6 `002A44B8`, 7 `002A4500`, 8 `002A4548`, 9 `002A455C`, 10 `002A4008`. Ten targets were recognized as instructions; **phase 4 target `0x002A4334` lacks a Ghidra instruction**. Version 1 intentionally refused with `NO_INSTRUCTION_AT_TARGET phase=4 target=002A4334`, resulting in `CHANGES (0)` and **no database modification**. This is a *disassembly/analysis gap*, not evidence that the phase is absent from the native image.

Generated `DC2_US_HDD_Installer_Switch_Repair_20261010_v2.java` in the current conversation (not yet run in Ghidra). It binds the exact eleven verified target addresses and checks `Ver0.309\\0`; at the *single known missing* entry, prints four raw bytes and existing code-unit type, refuses if defined data/instruction overlap or wrong memory permissions, then uses Ghidra 12.1.3 `DisassembleCommand` restricted to **four bytes**, `followFlow=false`. On success and only after decoding, restores function-body ownership, adds the computed jump refs and labels, and verifies every table target is an owned instruction. The entire sequence is transactional and rolls back on conflicts, preserving existing data/instructions and all executable bytes. Do **not** assume it compiled or committed yet; if it fails, use its end-of-log `UNRESOLVED / INVALID STATES` and raw bytes to decide the smallest next repair rather than clearing any data blindly.

### Verified v2 US installer switch repair: committed (2026-10-10)

The user executed `DC2_US_HDD_Installer_Switch_Repair_20261010_v2.java` in `SCUS_972.13` and reported **`Ghidra metadata repair COMMITTED`**, **`CHANGES (10)`**, **`UNRESOLVED / INVALID STATES (0)`**. The single previously undecoded jump-table target `0x002A4334` contained raw bytes **`F0 01 01 3C`**, decoded as **`lui at,0x1f0`** (one 4-byte instruction; metadata-only). The function body is now `0x002A3A80..0x002A4617`. All 11 phase-entry destinations resolve to instructions contained in the repaired function, with no other-function ownership conflicts. The script added two previously missing computed-jump references from `0x002A3FF8` to `0x002A45EC` and `0x002A4334`; the other nine were already represented, so do not interpret `CHANGES(10)` as an incomplete repair. Added non-primary labels `Dc2Title_HddInstallerPhaseJumpTable_US @ 0x0036E2D0`, `Dc2Hdd_InstallVersionMarker_US @ 0x00372E20`, and `Dc2Hdd_InstallHeaderResource_US @ 0x00372ED0`; EOL comments added at `0x002A3FF8`, `0x00372E20`, and `0x0015C374`. **Executable bytes and pre-existing instructions/data preserved.** This verifies the state-machine metadata fix, **not HDD driver loading or install success**. Do not rerun or repeat this completed repair.

**Next action:** the existing read-only `DC2_US_HDD_Loader_Resources_And_CodeCaves_20261010_v1.java` report, specifically observed source archive/partition identifier strings and any conservative candidate executable payload locations, is the next required live Ghidra evidence before designing the driver-loader hook. The untouched zero padding `0x0015C398..0x0015C39F` (8 bytes) is too small to contain a loader. Use a branch/callsite plus a verified separate loader location; do not substitute bypassing `HDDINFO` or modifying virtual HDD without backup. Existing JP Codex lifecycle metadata and v5 reports are sufficient; do not request another archive.

### US transfer strategy

1. Verify US title/installer/backend routines one by one against the annotated JP flow, not by applying a global address delta or automatically copying 153 signatures. The original Metrowerks symbols and all existing unresolved US ABI parts must be preserved.
2. Compare the JP init/driver-loader call path against US `init__Fv @ 0x0015C160`; the US title menu is already confirmed exposed by the one-word patch, but the HDD option was greyed out without a resident IOP `hdd` module.
3. Inspect US `DATA.HD3`, `SYSTEM.CNF`, version-marker/partition path constants, native filesystem lifecycle, and English installer assets. The installer's logical compatibility depends on these, not just on four successful IRX loads.
4. Investigate a minimal, reversible way to load DEV9/ATAD/HDD/PFS with the verified JP startup argument bytes and to initialize EE filesystem callbacks before enabling real install actions. **Do not bypass `HDDINFO` checks or write to a non-backed-up HDD image.**
5. Independent experimental artifact `DC2_US_HDD_Confirmed_Metadata_Apply_20261010_v1.java`: strict read-only-binary Ghidra script, guarded against wrong program/image-base/stock-gate bytes. Adds ten **non-primary** alias labels, contextual comments and three nonconflicting four-byte status globals in US Ghidra, without changing function signatures, existing defined data, or executable bytes. Prepared in this chat but **not runtime-tested in Ghidra**. Report unresolved states at the end; never blindly transfer JP structs to US.


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
