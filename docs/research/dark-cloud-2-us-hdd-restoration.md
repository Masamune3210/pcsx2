# Dark Cloud 2 (US) native HDD installation restoration

Status: **v5 clean HDD restoration stable in tested PCSX2 setup following 4,855-second gameplay session; native US installation and HDD-backed gameplay operational. Physical hardware and unusual reset paths not verified**  
Repository branch: `research/dark-cloud-2-us-hdd-restoration` (from `master`)  
Baseline: *Dark Cloud 2*, USA v2.00, `SCUS_972.13`, PCSX2 2.9.115, game CRC `1DF41F33`, ELF 4,014,996 bytes.

## US v1.00/v2.00 direct disc-file comparison (2026-10-11)

**TCRF diff detail supplied by user (2026-10-11):** The screenshot's `en v100 vs v200` block contains **exactly one changed US DATA path**: `DATA/map/i/i04/h01/i04h01_1.stb`. Do not misattribute the larger list of changed `e314_monica.chr`, `e315_monica.chr`, `e316_monica.chr`, `e317_monica.chr`, `g.lnk`, `s17.stb`, and `nk` to America: they appear under the **separate Japanese `jp v101 vs v201`** block. The screenshot is a reported TCRF diff of DATA paths, **not a comprehensive audit of `SYSTEM.CNF`**, which the user's own Beyond Compare reported as different. Do not assert that the lone `.stb` is a game-code, installer, size, or bugfix change without inspecting contents and byte size. This narrower US DATA delta lowers the likelihood of the 1664-MiB installer fix needing revision but does not independently prove HDD installations made from the two revisions are interchangeable. **Patch instruction bytes are already shared due to the identical executable**, so no additional patch fork or mandatory Ghidra audit is justified. Still pending: version-specific runtime boot/loading and cautious installed-data compatibility validation.

**User-supplied new evidence:** Beyond Compare was run against both US disc folders. The user reports that **only `SYSTEM.CNF` and the `DATA` files differ**; all other files match. If the Beyond Compare session compared full file contents, the **`SCUS_972.13` executable, DEV9/ATAD/HDD/PFS IRX modules, and other game files excluding those named are byte-for-byte identical**. This is stronger than the previously observed Ghidra import metadata equality and independently corroborates it. No Beyond Compare session report, detailed list of changed DATA filenames, or `SYSTEM.CNF` diff has been provided yet; preserve the exact scope as user-reported, not machine-reverified by us.

The separately imported Ghidra archives for the v1.00 and v2.00 folders both record **MD5 `2d1aca0b2522a520a4e8839e88b7db69`** and **SHA-256 `ac784f749c046cc78ebc3d7a026342a505354c422ffc55187b8128983d7d74e7`**, language `r5900:LE:32:default`, original image base `0x00100000`. The checked file equality means the existing 127-write v5 restoration should target **exactly the same original EE instruction bytes** in both US releases; no separate executable-port or relocation is warranted. Under the content-comparison assumption, **binary patch compatibility can be accepted without running the six-site Ghidra audit**. Do not reintroduce an obligatory Ghidra-script verification step solely for patch sites: `patches/dark-cloud-2-us-hdd/DC2_US100_HDD_Compatibility_Audit_20261011_v3.java` remains available as optional provenance/comment annotation. It was authored but **not executed**, and must not be described as a passed test.

**Still open:** The unprovided `SYSTEM.CNF` differences could affect the boot configuration. The different `DATA` files are relevant to the HDD **installed payload content/size/version**, and to whether a v1.00 disc can share v2.00-installed game data. The 1664-MiB capacity fix was measured for v2.00; do not assume v1.00's install data size/marker compatibility just because code and IRX are identical. The US v1.00 disc has not yet been reported booted with v5, so distinguish **shared executable compatibility (strongly supported)** from **1.00 installation/gameplay runtime validation (pending)**. Next practical comparison: inspect the explicit changes in `SYSTEM.CNF` and identify/size the different `DATA` files, followed by a v1.00 cold-boot using the existing v5 patch, preferably with a backup HDD image and without overwriting an existing installed title before confirming revision compatibility.

**Proven EE anchors, unchanged across the identical executable:** `0x0015C374` originally `0C0523DC`; `0x0015C398` and `0x0015C39C` original padding `00000000`; `0x0029F2EC` original `A3808460`; `0x0031C4BC` original `2A42000B`; `0x0031BF40` original `2A220600`. These original values came from the existing v2.00 verified audit; their identity for v1.00 follows conditionally from the user's full-file equality claim, **not** from executing the v1.00 audit.

## Stable v5 gameplay validation (2026-10-10)

**New user-confirmed runtime milestone:** After replacing the diagnostic-heavy experimental restoration with the actual stripped-down **v5** `.pnach`, PCSX2 logged:

```text
[10882.8291] Add 4855 seconds play time to SCUS-97213 -> now 35768
```

This records **4,855 seconds (1 h 20 m 55 s)** added in the tested session, and the user reports the patch remained stable over that gameplay. Combined with the prior verified native US installation (1,664 MiB), working HDD-backed loading, and static rebuild/regressions, this is adequate to designate **v5 stable in the user's PCSX2 configuration**. The active v5 loader is the **488-byte, 127-write production patch** with no debug-state table; do not mistakenly attribute the validation to v4 or assume v5 has only been statically tested. Do not imply the timestamp `10882.8291` equals playtime; it is merely the log timestamp.

**Evidence boundary:** The log line establishes added game-play duration; the user reports stability. No full new v5 gameplay trace was attached for this milestone, so do not claim per-operation instrumentation or a separate measured cold-boot trace from this line alone. Physical PS2 compatibility, arbitrary EE heap lifetimes, unusual soft resets, and no-HDD fallback remain unverified, not known failures. HDD OSD metadata localization is postponed by user request; no changes made to the shipped OSD resources.

## Lean production candidate v5 (2026-10-10)

User objected to the v4 cleanup retaining actual experimental loader diagnostics in the `.pnach`. **v5 removes those debug instructions/data rather than merely editing comments.** Its new MIPS assembly emits a **488-byte** loader (SHA-256 `bea8b5981c092fa501acee8884db834c1605b52fe0110a5b13250963efda48d4`) instead of v4's 736 bytes, and the standalone patch has **127** EE word writes instead of 189. It contains **one patch group**: `[Dark Cloud 2 - HDD Support]`. Deleted: `DC2HDDv1` marker, stage counters, module-ID recording, failure/result storage, completion flag, and associated instructions. **Retained without changes:** title gate `0x0029F2EC`; original init call hook `0x0015C374`; trampoline `0x0015C398`; the two 1664 MiB instruction fixes `0x0031C4BC` and `0x0031BF40`. Functional startup stays DEV9/ATAD/HDD/PFS with the same paths and exact argument bytes, each module-ID/start-result check, `sceFsInit`, and original `InitCDFile` continuation. The code and data still reside temporarily at `0x01FE8000`: **no universal memory lifetime guarantee**.

**Verification:** rebuilt using clang/ld.lld/llvm-objcopy with exact SHA match; strict static review confirms every module path, argv block, control-flow failure branch, unchanged original init epilogue, API call ordering, and unchanged external hooks. **14/14 local regression tests pass.** This is **not the same executable payload as v4**. It has subsequently been **runtime-tested in PCSX2**, with 4,855 seconds of gameplay added and no failure reported by the user, establishing stable status on their tested PCSX2 setup. Keep the v4 reference in commit `70948ffc24d9258888a73bc69cd3c42bd3983f7e` for regression comparisons. The v5 cleanup removes the four v4 artifacts from the current tree, while their exact bytes remain in Git history. The active directory includes `DC2_US_HDD_Production_20261010_v5.pnach`, `DC2_US_HDD_Loader_Production_20261010_v5.S`, `DC2_US_HDD_Build_Production_20261010_v5.py`, `DC2_US_HDD_Production_Tests_20261010_v5.py`, and `README.md`.

**Remaining optional compatibility checks:** repeat cold boots, test game/IOP soft resets, title return, saved-game read/write, no-HDD fallback and physical PS2. These are separate coverage gaps, not blockers on the observed PCSX2-stable gameplay. No reinstallation of the existing 1664 MiB partition is needed.

## Why this branch exists

Investigate whether the native HDD installation/loading functionality available in Japanese *Dark Chronicle* can be made accessible in the US release. The US executable's Ghidra database retains HDD installation functions and apparent title-screen UI handling, despite no normally visible US install option. **Confirmed game-side restoration, not a PCSX2 defect:** the US title installer and HDD-backed gameplay run using the native game paths with reversible EE patches. The original installation allocation was insufficient for US assets.

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

## EE trampoline lifetime and host shutdown behavior (2026-10-10)

New runtime-injection design priority: use a **temporary EE trampoline** while game IOP startup is running; allow the EE scratch memory to be reclaimed later only if control has already returned and **no VSync patch rewrites it**. The four IRX files are **not copied into EE RAM**; JP uses `sceSifLoadStartModule` to load their own files from `cdrom0:\\MODULES\\*.IRX;1` directly into **IOP RAM**. The EE payload only needs code, driver-path strings and 11/17-byte argument arrays, plus stack state. The old US Ghidra scan found **zero unreferenced 0x400-byte initialized-code cavities**; absence of a cave is not a proof that transient EE storage is impossible. An arbitrary high EE address cannot be presumed safe: the game stores state around `0x01F062D0` and its heap/stack/asset allocations may use upper memory.

**Exact low-disruption US hook candidate, not yet patched:** `init__Fv @ 0x0015C160`, **`0x0015C374`: `jal InitCDFile__Fv @ 0x00148F70`**, followed by 0x0015C378 NOP delay slot. Original opcode bytes **`DC 23 05 0C`**. Instead of overwriting a loader loop, an EE pnach can replace just the `jal` target with a temporary trampoline; the trampoline would (a) preserve incoming ABI/callee-saved registers and `ra`, (b) load DEV9/ATAD/HDD/PFS in JP order using US `sceSifLoadStartModule @ 0x00117D58` and exact 0xB/0x11 argument bytes, (c) call US `sceFsInit @0x00114008` after success, (d) call original `InitCDFile @0x00148F70`, then (e) return to **`0x0015C37C`** using the original `ra`. Verify JP startup ordering/call branch, especially whether `PreparePowerOff` occurs before `sceFsInit`, prior to changing guest. Do not use the eight-byte NOP padding after `init` as a payload cave.

**PCSX2 fork inspected:** `pcsx2/VMManager.cpp::VMManager::Shutdown` directly stops the VM, closes IOP and DEV9, calls `DEV9close()` / `DEV9shutdown()`. `pcsx2/DEV9/DEV9.cpp::DEV9close()` calls the emulated ATA device's `Close()`. Neither observed host-side shutdown function explicitly invokes the guest EE `Dc2Hdd_PowerOffThread`, so **normal emulator Stop is not the guest's PS2 power-button callback**. For an emulator-only first proof of HDD-driver load, the JP `PreparePowerOff`/power-button worker can be **deferred**, but `sceFsInit` cannot be dropped on that justification because it initializes the filesystem RPC bindings. Do not drop the game IOP reboot; the driver startup is meant to run after `IOPRP243.IMG` resets IOP. If stopping PCSX2 in mid-install, host ATA close doesn't guarantee filesystem-level consistency: always use backed-up virtual HDD, and avoid stopping during writes.

PCSX2 `pcsx2/Patch.h` and `pcsx2/Patch.cpp` confirm the four pnach modes: **0** boot-only, **1** every VSync, **2** boot + every VSync, **3** boot/GUI activation. For a transient EE shellcode/payload **never use 1 or 2** after it could be repurposed by guest allocations; otherwise continuous code writes can corrupt active game memory. Prefer **0** for testing startup hook/payload after validating patch-install timing, with individually selected gate patch timing as needed. Patch target must be applied after ELF load but before US `init` reaches `0x0015C374`; verify this in the emulator debugger. Any proposed target address needs runtime proof that the guest does not touch it **during execution** and that writes did not clobber existing code/data. A region being overwritten **after trampoline return** can be valid for startup-only payload if no later code still references it. No payload address selected and **no executable boot-loader pnach constructed or tested** yet.


## IOP RAM and runtime timing guardrails (2026-10-10)

### Constraint clarification: normal memory topology, boot time nonissue (2026-10-10)

The user confirmed the design requires **standard retail 32 MiB EE + 2 MiB IOP memory visibility**, not because an emulator-only patch is inherently undesirable, but because games can depend on memory ceilings, mirrored address behavior, fixed heap/stack layouts, and availability probes. Exposing extra emulator RAM can hide a genuine overflow or produce unrelated behavior changes; **do not enable extra memory even as the baseline verification route**, and never treat an 8 MiB IOP success as proof of compatibility. The user explicitly **does not prioritize reducing boot-time initialization cost**; correctly loading four modules before gameplay is preferable to optimizing startup speed. Focus time/performance validation on **resident IOP allocations, fragmentation, interrupts, scheduler contention, and steady-state resource access**, not how quickly the boot finishes. The user cited US *The Sims 2* retaining HDD functionality as a practical precedent; do not extrapolate its driver footprint to *Dark Cloud 2* without measurement.



The critical resource constraint is **resident IOP RAM**, separately from temporary EE shellcode. This PCSX2 fork defines `Ps2MemSize::IopRam = 2 MiB`, `MainRam = 32 MiB` in `pcsx2/MemoryTypes.h`. `pcsx2/Memory.cpp` initializes `ExposedIopRam = IopRam`; its extra-memory mode can expose **8 MiB IOP** and **128 MiB EE** but that mode must **not** be relied upon for retail-compatible restoration. `pcsx2/IopMem.cpp` memory LUT uses `ExposedIopRam`. In the Japanese game, DEV9, ATAD, HDD and PFS are independently loaded into IOP RAM and remain resident while active; injecting `sceSifLoadStartModule` path strings into EE **does not mean four IRX binaries are carried in the EE shellcode**. Do not patch until the IOP module load IDs and start-result codes are observed at runtime in **2 MiB IOP mode**.

**Measurement plan at IOP startup checkpoints:** (1) after IOPRP reboot, (2) after existing ten game modules, (3) after each DEV9/ATAD/HDD/PFS addition, and (4) once normal gameplay starts. Capture module list and memory ranges, total **and largest-contiguous** available IOP memory block (fragmentation matters), thread stacks, and module load/start results. Compare equivalent JP and US boot sequences. No free-byte count or concrete IOP resident-footprint figures have been measured, so do not invent them. A success under emulator-only 8 MiB expansion would be insufficient.

**CPU/timing concerns:** the four additional module startups add a one-time boot cost; resident IOP modules also create possible ongoing interrupt/thread overhead. No performance penalty is established without measurement. Ordinary gameplay still selects the existing CD backend when not installed; after an install it redirects eligible resource I/O to PFS-backed HDD, which may reduce CD seek delays but add ATA/PFS work, EE↔IOP RPC waits, and scheduler contention. Compare cold-boot durations, guest IOP CPU/thread usage, PCSX2 frame time and emulation-speed logs, and representative CD-versus-HDD resource stalls in the same scenes; isolate startup costs from normal-gameplay costs and installer-only costs. The installer thread's pause logic only changes EE priority 10→11, not suspension, and install copy loops add I/O pressure while active. Avoid changing the virtual HDD during performance tests and use backup images.

PCSX2 `VMManager::Shutdown` is a host teardown, not a guest shutdown callback; its `DEV9close` closes the emulated ATA device. Deferring the JP `PreparePowerOff` handler for an initial proof-of-concept may be valid **only after confirming its omission does not affect other initialized routines**. Do not drop `sceFsInit`, and do not infer host ATA close guarantees guest PFS consistency mid-install.


## Minimal US eight-byte trampoline smoke test (2026-10-10)

Created a **boot-only .pnach proof-of-control-flow**: `DC2_US_HDD_Startup_Trampoline_Smoke_20261010_v1.pnach` (conversation artifact, 971 bytes, static encoding checks passed, **not yet runtime-tested**). This is a complete pnach with the previously working title-gate patch plus a new separate smoke-test group. For actual PCSX2 loading the user must replace/rename the existing file to **`SCUS-97213_1DF41F33.pnach`** (don't leave competing same-name files). No new game modifications in git.

**Verified from US v3 Ghidra native instruction report:** `0x0015C374` originally holds `jal InitCDFile @ 0x00148F70`, MIPS instruction `0x0C0523DC`, and `0x0015C378` is a NOP. The two following words at `0x0015C398, 0x0015C39C` are **eight zero bytes** in the padding after `init__Fv` and before `main`. That permits **exactly a two-word tail-jump smoke test**, not the production loader:

```ini
[Dark Cloud 2 - One-Shot Startup Trampoline Smoke Test]
patch=0,EE,0015C398,word,080523DC
patch=0,EE,0015C374,word,0C0570E6
```

`0x0015C398: j 0x00148F70` followed by existing NOP at `0x0015C39C`; `0x0015C374: jal 0x0015C398` preserves `ra = 0x0015C37C`, allowing original `InitCDFile` to return to normal US init at `0x0015C37C`. Both jumps stay in the compatible MIPS 256 MiB segment; little-endian encodings were independently asserted with a Python calculation. **No extra EE allocation, no IOP resident modules, and no altered game behavior expected.** Both new writes use `patch=0`, not a continuous VSync patch. To prove patch timing and execution, break on EE PC `0x0015C398` and inspect `ra=0x0015C37C`; then resume and verify game boots normally. Disabling the patch returns to stock instructions after a clean relaunch. The smoke test does **not** prove large future payload placement or IOP residency.

**After smoke test passes:** identify a finite-lifetime EE payload slot that is safe *during the four blocking module loads* (or choose an alternative controllable loader location, not devkit-only memory). Keep memory exposed at standard 32 MiB EE/2 MiB IOP, capture free + largest contiguous IOP memory before/after each real IRX load; compare against JP. Only then build the full US DEV9/ATAD/HDD/PFS + `sceFsInit` trampoline, preserving US `DATA.HD3`, `PP.SCUS-97213.APP.DARKCHRO` and `Ver0.309`. Do not implement or force runtime HDDINFO fallback before actual driver success.

### Trampoline execution confirmed; disposable EE payload versus live stack (2026-10-10)

User supplied PCSX2 EE debugger screenshot with an **execute breakpoint hit count 1** at `PC=0x0015C398` displaying `j InitCDFile(void) @0x00148F70`; the preceding native patched call at `0x0015C374` is `jal 0x0015C398`; `ra=0x0015C37C`. The boot-only .pnach **really executed its trampoline**, not merely registered in PCSX2. This closes the earlier smoke-test execution-verification gap. The screenshot also shows **`sp=0x01FFFD40`**. Retail 32 MiB EE RAM ends at exclusive `0x02000000`, so the upper bound is just **`0x2C0 = 704` bytes above the current SP**; the stack grows downward. Do **not** propose placing loader code or path/argument bytes blindly at the literal top of EE RAM: the live game stack is there, and nested module-load calls can extend downward into nearby high memory. Existing global data around `0x01F062D0` further invalidates the assumption that upper EE RAM is empty.

The proposed **finite-lifetime EE loader payload** is viable in principle: put EE code and constant driver paths in a memory region shown unoccupied **at the hook and throughout four blocking IRX startups**, run DEV9/ATAD/HDD/PFS + `sceFsInit()`, invoke original `InitCDFile()`, then return at `0x0015C37C`. After all synchronous uses and any EE→IOP transfers complete, the game may overwrite that discarded payload (never write to it with continuous VSync pnach). This does **not** permit collisions with live stack, ELF sections/BSS, heap, DMA or dynamically-used guest RAM *while the loader executes*. Check candidate EE range at runtime before patching it; verify boot-only patch survives game-side initialization writes, set range write watchpoints while the loader runs, and ensure no deferred references remain. No specific high-EE payload address selected yet; no production HDD-loader pnach generated or tested. Standard 32 MiB EE / 2 MiB IOP configuration remains mandatory.

## US one-shot trampoline smoke test: PCSX2 log (2026-10-10)

User supplied full 3,454-line PCSX2 v2.9.115 log from running `SCUS-97213` v2.00 CRC `1DF41F33` with the experimental patch. At log ~69.095s, PCSX2 explicitly lists the enabled patches:
```text
Enabled patch: Enable HDD Install Menu
  1,EE,word,0029f2ec,0
Enabled patch: Dark Cloud 2 - One-Shot Startup Trampoline Smoke Test
  0,EE,word,0015c398,80523dc
  0,EE,word,0015c374,c0570e6
Enabled patch: Widescreen 16:9
  1,EE,word,00138d78,3f023f40
```
The US game then loaded ten standard IOP IRX modules with IDs 25..34, reached title and FMV playback, then resumed a Chapter 2 save at Sindain Station. No fatal EE crash or VM failure shown; log ends at normal VM pause/shutdown (~133s). **This confirms patch registration + successful game startup, NOT yet actual execution of the injected instruction at `0x0015C398`**; requested EE breakpoint `0x0015C398` / `ra=0x0015C37C` is still not reported and remains the exact next smoke validation.

The log demonstrates two **different IOP generations**: HDDOSD-enabled BIOS **earlier** loads HDD-capable `dev9/atad/hdd/pfs` from `loadbuffer` at ~53.13-53.80s and uses the HDD image, including `pfs0:/osd100/hosdsys.elf`. BIOS then transfers to the game with IOP reboots at ~69.23-69.40s, destroying those BIOS-resident driver instances. Game boot afterward loads only ten ordinary modules; its `searchbyname: module name hdd` returns `-202` at ~77.385s and ~119.245s and `Unknown device 'hdd'`. **Never mistake BIOS initialization logs for successful Dark Cloud 2 HDD restoration**.

PCSX2 boot explicitly reports `IOP mode, 2MB` and `Total accessible memory size: 32 MB`; original retail memory limits honored. Three active game patches included an unrelated widescreen one; the log also warns `Fast CDVD is enabled`, which is an independent unsafe setting worth disabling during later disc/HDD correctness tests. Numerous `sceMidi_MidiPlaySwitch Error Not Stop` messages occur after module startup and did not prevent normal gameplay; not evidence of HDD-specific regression without baseline comparison.

**No new injection address has been authorized or proven.** Do not bypass HDD detection, load four IRXs, overwrite unmeasured EE memory, or claim the tiny 8-byte trampoline proves free memory. Next obtain debugger break at `0x0015C398`, examine `ra` and PC control flow, then select/prove a temporary EE loader memory range and test IOP driver residency within 2 MiB. No Ghidra changes are required for this smoke-test stage.


## Full US four-driver EE prototype and temporary-slot test (2026-10-10)

**Confirmed in the prior PCSX2 debugger:** original one-shot US call `0x0015C374 -> 0x0015C398` executed with `ra=0x0015C37C`. User suggested transient EE memory near the end of retail 32 MiB; must not collide with the observed live descending stack `SP=0x01FFFD40` or persistent globals around `0x01F062D0`.

**New actual deliverables, locally generated/verified but NOT PCSX2-runtime-tested:**

- `DC2_US_HDD_EE_Disposable_Slot_Smoke_20261010_v1.pnach`: boot-only four-write smoke test that chains the proven 8-byte stub `0x0015C398` to **provisional** `0x01FE8000`, where a 2-word `j 0x00148F70; nop` tail-calls stock InitCDFile without modifying IOP state. Validated all three MIPS J/JAL encodings. Proves *ability to execute there* only after debugger confirmation, not long-term scratch safety. Include the menu gate in same full .pnach; use this **instead of** previous .pnach, not concurrently.
- `DC2_US_HDD_NativeLoader_Build_20261010_v2.py`, `DC2_US_HDD_NativeLoader_Stage2_20261010_v2.S`, `DC2_US_HDD_NativeLoader_Stage2_20261010_v2.elf`, `DC2_US_HDD_NativeLoader_Stage2_20261010_v2.bin`: prototype loader built offline with Clang MIPS-III assembler and ld.lld, independently checked with llvm-objdump, readelf and Python opcode/argument validation. It preserves `ra, gp, s0-s3, a0-a3`, uses a 0xC0-byte stack frame, and calls existing US APIs (no extra memory mode): `sceSifLoadStartModule @ 0x00117D58` for DEV9/ATAD/HDD/PFS with JP verified 11-byte HDD and 17-byte PFS argument buffers; `sceFsInit @0x00114008`; stock `InitCDFile @0x00148F70` on success or failure. On module ID negative or start-result 1 it **does not spin forever** like JP, instead records a diagnostic failure code, skips remaining initialization and calls original InitCDFile. No fake HDDINFO; no JP poweroff worker. 736-byte EE code+data payload at **candidate** `0x01FE8000..0x01FE82DF`; code and driver paths precede `0x01FE82B8` status header. The user must first validate this temporary slot during *the entire loader execution*; a few hundred bytes of arbitrary unallocated-looking memory do not establish safety. The module files' existence on the US DVD **is not yet established**; failed calls should be observable.
- `DC2_US_HDD_NativeLoader_Experimental_20261010_v2.pnach` is a complete **200-line boot-only** experimental payload (code writes then stub-hook writes), not runtime tested. v2 fixes the start-result==1 diagnostic status, using a delay-slot move to report result 1 instead of misleadingly reporting positive module ID. Prefer **v2** over generated v1. `DC2_US_HDD_NativeLoader_Validation_20261010_v2.txt` contains exact code size, symbols and debug addresses.

**Checkpoint order:** (1) use disposable-slot smoke pnach, set EE execute breakpoint `0x01FE8000`, expect `ra=0x0015C37C`, and verify normal title/game launch; (2) establish candidate slot is not written/clobbered during the loader-length synchronous IOP operations (write watch `0x01FE8000..0x01FE82B7` while code runs, excluding `0x01FE82B8..` diagnostic words, and account for DMA); (3) only then replace smoke file with experimental v2 full pnach on backup HDD; (4) break immediately after loader at `0x0015C37C` and inspect diagnostic memory at the following absolute EE addresses: header `0x01FE82B8`, last successful phase `0x01FE82C0`, failure result `0x01FE82C4`, `sceFsInit` result `0x01FE82C8`, return complete flag `0x01FE82CC`, individual module IDs `0x01FE82D0..0x01FE82DC`. `phase=5` and complete=1 are the success signature. Normal 32 MiB EE/2 MiB IOP configuration only; backed-up HDD. If US IRX files are unavailable, fall back to normal initialization, investigate physical disc module inventory and alternate driver supply, don't force HDDINFO. `Fast CDVD` remains an unrelated unsafe option to disable during correctness tests. These files are local conversation outputs, not committed PS2 ELF/PCSX2 source changes. Both pnach files require renaming to the installed game's expected `SCUS-97213_1DF41F33.pnach`, with **only one enabled at a time**.

### Disposable EE slot smoke log: normal gameplay (2026-10-10)

User ran **only** `DC2_US_HDD_EE_Disposable_Slot_Smoke_20261010_v1.pnach` and supplied complete **PCSX2 v2.9.116** log `Pasted text(20261010-194010).txt` (3,398 lines). The emulator **recognized and enabled** the exact four boot-only writes at ~26.855s: `01FE8000=080523DC` (`j InitCDFile @0x00148F70`), `01FE8004=0`, `0015C398=087FA000` (`j 0x01FE8000`), `0015C374=0C0570E6` (`jal 0x0015C398`). The **existing separate title HDD gate was not enabled** in the log; only the disposable-slot smoke group and unrelated widescreen group appear. This has no effect on testing EE memory placement.

The game finished loading all ten ordinary CD IOP modules (IDs 25–34) ~27.21-27.51s, played an FMV, and continued into a Chapter 2 Sindain Station save for >10 seconds. VM paused at ~68.230s and closed DEV9 normally; **no EE crash/fatal exception**. IOP `searchbyname hdd` still returned `-202` at ~51.783s as expected because this was **not the full driver loader**. BIOS-loaded HDD modules before game IOP reboot are unrelated. PCSX2 logs boot patch registration but no PC trace at `0x01FE8000`; the earlier tiny-stub `0x0015C398` breakpoint and lack of boot regression together strongly support the new trampoline executing, but **an execute breakpoint at `0x01FE8000` remains the definitive evidence**. Normal gameplay with the tiny 2-word payload does **not** verify all `0x2E0` bytes remain unmodified across the four blocking IOP module loads.

Offline check on existing `DC2_US_HDD_NativeLoader_Stage2_20261010_v2.S` confirms the US `sceFsInit @0x00114008` decompilation returns `0` on success and negative values on failure, so the prototype's result check is consistent with the exported ABI. No full-loader test yet; don't claim IOP-resident modules or restored HDD detection from this smoke log.

Next: if possible breakpoint **EE execute `0x01FE8000`** and confirm `ra=0x0015C37C`, then evaluate temporary range writes for whole intended `0x01FE8000..0x01FE82DF` region and test existing **v2** HDD driver loader only on the backed-up disk/retail 2 MiB IOP. The US disc IRX file inventory remains unverified; failure of path lookup is a plausible failure independent of trampoline. Avoid requesting new US .gzf or repeating known Ghidra switch repair. `Fast CDVD` is still enabled and should be disabled for later filesystem correctness tests.


### US HDD installer visibly progressing (2026-10-10)

User supplied PCSX2 game-window screenshot (2026-10-10 19:44) showing the **native English US Dark Cloud 2 installer** with `Now installing.` progress bar approximately **45%** and warning `Please do not turn off the unit or open the CD tray. You may cancel the installation by pressing O.` This is **visibly active installation**, a stronger runtime milestone than the earlier menu exposure and EE smoke tests. **The screenshot does not identify which .pnach was active**, nor show final completion, module-return diagnostics, IOP free memory, the persisted `Ver0.309` marker, or post-install HDD resource routing. Do not assume the earlier **smoke-only** patch could load modules, nor credit experimental native-loader v2 without confirming the user's active patch/log. No evidence of installation completion or successful re-launch yet. Next verify actual .pnach variant and full log after allowing the installation to finish; inspect native installation final state, marker, exit, clean reboot and HDD-backed asset reads on backed-up disk. **Never interrupt a working installation just to take debug measurements.**

### US installer v2 loader confirmed by user (2026-10-10)

The user **explicitly confirmed** the 2026-10-10 English US `Now installing.` progress-bar screenshot was taken while running the generated **`DC2_US_HDD_NativeLoader_Experimental_20261010_v2.pnach`**, **not** the earlier disposable-slot-only smoke test. This elevates the screenshot to a direct native EE injected four-module initialization test. At the time of the screenshot the native US installer had entered **actual progress / asset-copy mode**, visibly approximately 45%. The US game has retained its installation state machine, English menu/dialog resources, source archive `DATA.HD3`, partition ID `PP.SCUS-97213.APP.DARKCHRO`, and `Ver0.309` check. From entry into native installer copying it is strongly supported that game IOP DEV9/ATAD/HDD/PFS loading and PFS access proceeded far enough to mount/write the target; **do not claim individual module IDs/results or verified 2 MiB IOP free-block sizes until diagnostics/logs are collected**. Original emulator log confirms retail **2 MiB IOP** mode; no extra RAM was enabled. The user noted unfinished player-facing English wording in this preserved native installer (e.g. `DC2`, `installing the application`, `the unit`, `CD tray`): user-observed tone suggests an incompletely polished localization; it doesn't by itself prove precisely *when* the regional feature was cut.

**Current experiment is running:** allow native install to finish and flush, **do not recommend interrupting or swapping patches while writing**. Obtain normal completion/error UI, final PCSX2 log and module start diagnostics, confirm `Ver0.309` persisted, stop cleanly, cold-boot with **the same experimental v2 loader and backed-up HDD** and test full HDD presence in title, device switching, PFS-backed real scene/asset load and stability. Check post-loader native EE status words if needed: `0x01FE82C0` phase, `0x01FE82C4` failure, `0x01FE82C8` `sceFsInit` result, `0x01FE82CC` completion flag, `0x01FE82D0..DC` four module IDs, but remember disposable EE memory can be reused later; preferably read immediately on return from startup `0x0015C37C`. A successful installer screenshot does **not** prove EE temporary scratch remains safe in all future boot scenarios or all installed gameplay.


### US installation capacity failure and 1664-MiB patch builder (2026-10-10)

**New runtime evidence:** the user's **experimental four-module loader v2** booted the native US HDD installer and copied files until near the end, when the installer displayed **Installation Failed**. PCSX2 log was lost after a restart. wLaunchELF v4.43x_isr lists **both distinct partitions** `PP.SCUS-97213.APP.DARKCHRO` and `PP.SCPS-15033.APP.DARKCHRO` on the same 40,960 MiB formatted PS2 HDD, disproving earlier speculation that normal installer failure had removed the US partition. Selecting the US installation partition displayed **PFS SIZE 1536 MB / USED 1536 MB / FREE 0 MB**. The previously completed JP partition ended with roughly **22 MB free (~1%)** in the same 1536-MiB allocation. This is strong evidence for **PFS capacity exhaustion during a later file copy**, although the final installer return code was not captured; do not claim an exact failing `sceWrite` syscall until instrumented. The user observed `DATA.HD3` is about **265 KB** in US versus **252 KB** in JP; those are **index file sizes**, not direct measurements of copied payload bytes, and ordinary localization is a plausible contributor.

**Existing US decompilation independently confirms the minimal edit:** `InstallLoop__Fv @0x0031C270` creates its initial 128-MiB APA partition with `sceOpen`, then loops `sceIoctl2(fd,0x6801,...)` **11 times** (`iVar5 < 0x0b`), and `sceFormat(..., 0x2000 zone, ...)` then copies `DATA.HD3` records. `CheckInstallSpace__Fv @0x0031BE60` independently compares free MiB to **`0x600` (1536)**. Proposed reversible correction is **11 -> 12 additional 128-MiB subpartitions**, total **13*128=1664 MiB (0x680 MiB)**, plus `CheckInstallSpace 0x600 -> 0x680`. The extra 128 MiB sufficiency remains unverified, but it is the smallest practical increment and does not affect PS2 EE/IOP RAM sizes, virtual HDD geometry, IRX startup, or the US installation marker.

Generated **`DC2_US_HDD_Partition_Capacity_Patch_Builder_20261010_v1.java`** for Ghidra 12.1.3 US `SCUS_972.13` (read-only, **not yet executed in Ghidra**). Script finds a unique MIPS `slti/sltiu imm=0x0b` **between verified** `sceIoctl2` and `sceFormat` call sites in `InstallLoop`, and a unique `slti/sltiu imm=0x600` in `CheckInstallSpace`; preserves other 16 opcode bits and changes only immediate fields. Abort if anything is ambiguous/missing; logs invalid states at bottom. Only on success it asks for the **already-working v2 experimental loader pnach**, verifies hook and payload signatures, then writes a **new, separate merged pnach** containing all existing loader writes plus two boot-only capacity edits. It never overwrites the input or modifies the Ghidra DB. The script does not have enough evidence to publish hardcoded patch addresses until run on US live disassembly; do not invent addresses, or mistake the distinct **installer UI phase dispatch** `0x002A3FDC sltiu ... 0x0b` for the partition expansion bound.

**Next checkpoint:** user runs new Ghidra builder, confirms the two exact instruction addresses and .pnach output. Only then, after ensuring the HDD image backup is intact, remove **only the failed US partial** `PP.SCUS-97213.APP.DARKCHRO` (do not touch JP or other partitions) using normal wLaunchELF/HDD manager facilities, and run a fresh native install with **the single new merged patch**. Keep original retail memory limits, guard against disk writes mid-copy. Verify PFS free space, final `Ver0.309` marker, cold-boot installed file routing. Avoid deleting the partition merely to collect more logs; Ghidra instrumentation or a persistent PCSX2 console file can be addressed separately if failure recurs.


### Capacity builder v1: verified US instruction addresses (2026-10-10)

User **ran** `DC2_US_HDD_Partition_Capacity_Patch_Builder_20261010_v1.java` in the live US `SCUS_972.13` database; **two unique matches, zero unresolved/invalid states, and no Ghidra/executable modifications**. Output:
```text
InstallLoop @ 0x0031C270, body 0x0031C270..0x0031C933
sceIoctl2 call @ 0x0031C48C; sceFormat call @ 0x0031C524
0x0031C4BC 2A42000B slti v0,s2,0xB -> 2A42000C slti v0,s2,0xC
CheckInstallSpace @ 0x0031BE60, body 0x0031BE60..0x0031BF5B
0x0031BF40 2A220600 slti v0,s1,0x600 -> 2A220680 slti v0,s1,0x680
patch=0,EE,0031C4BC,word,2A42000C
patch=0,EE,0031BF40,word,2A220680
```
New expected partition allocation: one 128-MiB main plus **12** 128-MiB additions = **1664 MiB** (instead of 1536 MiB). The builder merged these two boot-only edits into the existing experimental four-driver-loader .pnach and emitted `C:\Users\Johnny\Downloads\exp.pnach`, reporting `PATCH_COMMANDS_ADDED=2; original driver-loader commands kept unchanged`. The builder did not runtime-test the merged patch. Preserve the existing menu-gate and loader instructions; only the merged .pnach should be activated for the game's CRC, `SCUS-97213_1DF41F33.pnach`.

**Next controlled runtime test:** ensure the 40-GB HDD image backup is verified; use the partition manager to delete **only** incomplete `PP.SCUS-97213.APP.DARKCHRO`, preserving JP `PP.SCPS-15033.APP.DARKCHRO` and all other partitions. Cold boot with merged patch, confirm PCSX2 lists the four-driver-loader group plus both capacity edits, then reinstall at retail EE=32 MiB/IOP=2 MiB. One extra 128-MiB subpartition is a plausible fix to the observed 1536-MiB full/0-MiB free late failure, **not** proven sufficient until completion. Verify installation marker `Ver0.309\0`, resulting PFS size/free space, cold restart and actual HDD-file asset loading. In any repeat failure, capture worker `RetCode` or file log before reset and avoid interrupting filesystem writes.

### US HDD native installer completed successfully (2026-10-10)

**Major confirmed runtime milestone:** The user supplied a PCSX2 screenshot after retrying the native US HDD installer with the merged experimental DEV9/ATAD/HDD/PFS loader and the Ghidra-verified extra 128-MiB subpartition capacity patch. **The game displayed `Application data installed.` in the original English installer UI.** This is the first observed **successful completion** of the region-disabled US *Dark Cloud 2* native HDD installation. Earlier attempt with stock 1536-MiB allocation displayed `Installation Failed` near end and wLaunchELF confirmed US PFS **1536/1536 MiB used, 0 MiB free**. New successful configuration was designed to create 1664 MiB (one 128-MiB main + twelve 128-MiB subpartitions) by patching:

```text
patch=0,EE,0031C4BC,word,2A42000C  # InstallLoop: slti v0,s2,0xC, 12 extra APA slices
patch=0,EE,0031BF40,word,2A220680  # CheckInstallSpace: slti v0,s1,0x680, 1664 MiB
```

**Interpretation:** Native installer reached its success UI after archive copy and finalization; previous US capacity-exhaustion hypothesis is supported by successful rerun. Do **not** yet claim cold-boot resource switching, actual game assets reading from HDD, or verified marker bytes and final partition free-space until independent test. The extant `InstallLoop` decompilation writes the nine-byte `Ver0.309` installation marker at the end, but the screenshot itself only shows successful UI, not marker bytes. Experimental patch remains necessary unless equivalent functionality is integrated elsewhere.

**Next test sequence:** after clean emulator shutdown, inspect the US partition identity **`PP.SCUS-97213.APP.DARKCHRO`** in wLaunchELF and note its PFS size/free space. Cold boot US game with **same merged .pnach**, confirm title detects installation and/or use native HDD file-source selection, enter actual gameplay and change scenes to exercise PFS-backed asset reads. If possible collect PCSX2 log evidence of game-side IOP module loads, PFS mounts/reads, or native `DefaultFileDev` transitions. Preserve backed-up HDD image and avoid reinstall unless needed. Consider collecting loader-status data only at startup before disposable EE memory can be reused.

### Post-install US PFS capacity verified: 1664/1553/111 MiB (2026-10-10)

User supplied a **wLaunchELF HDD manager screenshot after the native US HDD installer displayed `Application data installed.`**, reporting **`PFS SIZE: 1664 MB`, `PFS USED: 1553 MB`, `PFS FREE: 111 MB`, `6% FREE`** for the completed installation. The expanded APA/PFS allocation therefore **actually took effect**, not just the code comparison patches. Assuming wLaunchELF's MB measurements in its customary binary-MiB units, final used capacity exceeds the old **1536 MiB** limit by **17 MiB**, strongly corroborating the earlier near-end capacity exhaustion at 1536/1536 MB, 0 free. The +128-MiB subpartition leaves **111 MiB** free (~6.7%); no additional allocation is currently justified. `DATA.HD3` index file byte-size comparison did not by itself quantify archive payload. Treat the UI fields as rounded partition-level numbers, not exact payload bytes.

**Next unresolved milestone:** cold boot the US title with the **same merged experimental four-IRX loader + 1664-MiB patch** and confirm the game recognizes the installed `Ver0.309` marker and **actually loads assets through the HDD-backed PFS path**. Capture `DefaultFileDev` state or PCSX2 game-side PFS reads if unclear. Do not reinstall or adjust memory solely for this test. Protect working HDD-image backup, keep partial vs installed US/JP partition identities distinct.

### Runtime HDD-backed gameplay observed after cold boot (2026-10-10)

**Functional runtime milestone confirmed by user:** after successful native US HDD installation with the merged four-driver loader and 1664-MiB partition patch, the user cold-booted the game and played into **Chapter 2, Sindain Station**. The supplied PCSX2 log snippet showed **game-side DEV9/ATAD/HDD/PFS modules loaded successfully**, with HDD module **id 37, ret 0**, and `sceSifSearchModuleByName("hdd")` resolving **37** repeatedly rather than the unmodified US game's **-202**. HDD DMA reads continued into live gameplay and scene transitions. The user independently reported a **marked increase in loading snappiness**, consistent with actual PFS-backed gameplay asset loading. Together these are strong practical evidence that the restored US game uses its installed HDD data, not just a mounted but inactive partition. No instruction-level `DefaultFileDev` trace or per-asset PFS file-open trace has yet been captured, so reserve that narrower claim for instrumentation if ever necessary.

**Validated restoration scope:** (a) title menu exposed, (b) native EE initialization of four game-side IOP drivers via boot-only disposable loader, (c) genuine native HDD install succeeds after additional 128 MiB APA subpartition, (d) completed US installation uses **1553/1664 MiB, 111 MiB free**, and (e) game detects HDD and exhibits ongoing HDD reads plus improved load times during gameplay after cold reboot. This is sufficient to treat **HDD-backed gameplay operational**; remaining work is robustness/polish, not proof that basic install works. Maintain user's backed-up image and original 32-MiB EE / 2-MiB IOP memory limits.

**Outstanding if later requested:** investigate sustained use of high EE temporary loader slot, reliability across game resets/reboots, startup failures, patch packaging, any region-specific in-game issues, and potentially `DefaultFileDev`/PFS call tracing for formal source attribution. Do not require tracing solely to repeat an already established functional gameplay observation.

### Known-good v1 loader packaged and log-audited (2026-10-10)

**Implementation and release packaging completed offline (not yet merged to GitHub tree):** `DC2_US_HDD_Restoration_20261010_v3.pnach` is a consolidated, byte-for-byte verified reproduction of the **actual working** runtime patch: v1 temporary EE loader at `0x01FE8000` (`0x2E0` bytes) + US title gate + two unique Ghidra-verified 1664-MiB capacity edits. **Do not silently replace this with v2**: the supplied successful boot/gameplay PCSX2 log explicitly activates `0x01FE82BC=0x31764444` (`DC2HDDv1`), and v1 branch delay-slot words at `0x01FE8084/80D8/8130/8188` are zero. V2 differs at those words and only changes failure-code diagnostics, so successful gameplay has confirmed v1, not v2.

Generated deliverables (all uniquely named in the current chat):

- `DC2_US_HDD_Restoration_20261010_v3.pnach` (189 unique EE writes; all match active patch commands in user's `Pasted text(20261010-203803).txt` including both capacity words).
- `DC2_US_HDD_Loader_KnownGood_20261010_v3.S` (original successful v1 loader assembly, no semantic modifications).
- `DC2_US_HDD_Restoration_Packager_20261010_v3.py` (rebuild from MIPS `.S` via clang/ld.lld/llvm-objcopy, fail-closed SHA check, static patch audit and PCSX2 activation-log audit).
- `DC2_US_HDD_Restoration_Release_Notes_20261010_v3.md` (usage, verified evidence, limits and next regression tasks).
- `DC2_US_HDD_Restoration_Repo_20261010_v3.patch` (unified **git apply** patch adding the four artifacts under `patches/dark-cloud-2-us-hdd/`; user can apply to this research branch; no copyrighted game, IRX, BIOS or HDD bytes).

Validation output: compiled MIPS payload length **736**, SHA-256 **`b8f2bf8a1a974c6bcc505ac9fbaafbc9f396a665628c47925354c80af4861ef1`**, exact byte match against the earlier v1 assembler binary and **189/189 expected active writes** in the user's October 10 post-install PCSX2 log. Negative tests verified verifier rejects duplicate-address writes, missing capacity fix, and changed `DC2HDDv1` bytes. The git patch has been tested using `git apply --check`, `git apply`, and byte-for-byte comparison of four resulting files in a temporary local repository. **No change to working loader instructions** was made for packaging.

**PFS cache warnings observed in cold-boot excerpt:** Seven `pfs: warning: null buffer returned` messages are visible in the user's 8986-line PCSX2 excerpt: one during HDD-OSD before game launch (~1315.6842s), and six during the game's HDD-active period (~1353.2878-1353.3655s). The warnings are not accompanied by a reported crash or user-visible gameplay failure. Their presence under the preceding OSD PFS driver means they cannot automatically be attributed to this loader, but their later occurrence with the game driver merits focused follow-up if stability problems occur. Do not claim a PFS buffer leak or corruption from this text alone. The game driver at startup uses the known Japanese arguments and printed its actual buffer configuration in the log.\n\n**Open next step:** apply repository patch to `research/dark-cloud-2-us-hdd-restoration` and commit once possible; the branch handoff records release readiness but the files are not yet present in the remote branch. On the user's machine, verify consolidated `v3.pnach` with one additional cold boot and existing installed partition (no reinstall) if transitioning off the previously tested merged `exp.pnach`. Scratch-slot lifetime under arbitrary resets and real hardware remains unverified. Existing save/load, source-selection and menu-return regressions remain to run. Do not conflate successful package static audit with a newly performed runtime regression.

### Japanese HDD browser title from completed US installation (2026-10-10)

The user supplied a PS2 HDD browser detail screenshot for the **successfully installed US** Dark Cloud 2 application data. The browser shows **`ダーククロニクル アプリケーションデータ`** ("Dark Chronicle Application Data") **in Japanese**, lists **`Location HDD`**, **`Type PlayStation®2 Format Software`**, and **`Size 1,664 MB`**, and gives the install creation date **2026-10-10 2:14:50 PM**. The Japanese label is **user-facing game-specific HDD installation metadata**, not the English US title *Dark Cloud 2*, and matches the 1664-MiB allocation added by the restoration. Combined with regional serial `SCUS-97213` but surviving partition suffix `APP.DARKCHRO`, English installer wording including `DC2 application data` and `PlayStation BB Unit`, the US-specific marker `Ver0.309`, and the original 1536-MiB allocation being too small for the US contents, the screenshot supports **incomplete HDD feature regional localization**. It does **not** independently establish exactly when the feature was disabled. **Source class and persistence are established:** the PS2 HDD browser title and icon are installed as OSD/APA partition metadata when `InstallLoop__Fv @0x0031C270` creates the installation, not sourced dynamically from the runtime executable on subsequent browser visits. Prior installer tracing identified the HDD header resource `etc/hdd_head.hed` and its incorporation of `SYSTEM.CNF`, then the installer writes its assembled header to the HDD before creating subpartitions and formatting PFS. The exact **source field, file and byte offset** for the Japanese title have not yet been independently isolated, so avoid asserting which individual asset contains the literal without checking. This corrects the prior suggestion that the title's origin or persistence mechanism was unknown.

### Japanese HDD OSD uninstall text preserved in US install (2026-10-10)

The user supplied another HDD OSD screenshot showing that the **deletion/uninstall message for the installed US `PP.SCUS-97213.APP.DARKCHRO` remains Japanese**, in addition to the already verified Japanese two-line application title. This strongly supports an unlocalized shipped HDD **presentation asset**, not merely retained internal symbol or partition naming.

**On-disk format, independently cross-checked against `ps2homebrew/hdl-dump` `apa.h`/`apa.c` and `ps2homebrew/HDLGameInstaller` `OSD.h`/`OSD.c`:** a raw APA partition header is at the main partition start sector; the OSD parent partition attribute-area table begins **0x1000 bytes (4096 bytes) into the main partition**, signature `PS2ICON3D` at table offset 0, `version` at +0x0C, little-endian resource `(offset, length)` pairs beginning at +0x10, indexed **0 `system.cnf`, 1 `icon.sys`, 2 `list.ico`, 3 `del.ico`**. Resource offsets are byte-relative to the attribute table and 512-byte aligned; the attribute span is 4 MiB. Contrary to a previous paraphrase, the `PS2X` HDD `icon.sys` is a **text configuration** holding `title0`, `title1`, **`uninstallmes0`, `uninstallmes1`, `uninstallmes2`** and icon colors/lighting. `del.ico` is an icon model for deletion, **not the Japanese text**. The memory-card binary `icon.sys` split byte at 0x06 and title bytes at 0xC0 **do not apply** to this HDD resource; HDD text is decoded with Shift-JIS-compatible CP932. Source references: `https://github.com/ps2homebrew/hdl-dump/blob/master/apa.h`, `https://github.com/ps2homebrew/hdl-dump/blob/master/apa.c`, `https://github.com/ps2homebrew/HDLGameInstaller/blob/main/OSD.c`, and historical explanation `https://assemblergames-archive.com/post-580279.html`.

Prepared an independent, read-only **`DC2_US_HDD_OSD_Metadata_Inspector_20261010_v1.py`** (conversation artifact) that opens a raw uncompressed PCSX2 HDD image read-only, follows APA `next` LBA pointers starting at sector 0, matches **exactly** `PP.SCUS-97213.APP.DARKCHRO`, validates `PS2ICON3D` and descriptor boundaries, extracts the second attribute entry, and prints installed `title0/title1/uninstallmes0/1/2` decoded as CP932. Optional `--save-icon` writes only a separate new file, never the HDD image, and refuses overwrite. Sample invocation: `python DC2_US_HDD_OSD_Metadata_Inspector_20261010_v1.py --image "C:\\path\\to\\pcsx2-hdd.raw"`; the real image path is not yet known, so do **not** assume the sample path. **Static compilation and synthetic APA + PS2ICON3D + Japanese `PS2X` header round-trip passed**. It has **not** been run on the user's actual 40-GiB HDD image. Do not assume exact Japanese uninstall-message bytes or disc source offset until output is acquired; older handoff notes identify the install header template `etc/hdd_head.hed` but have not mapped the exact original text field inside it.

**Safety:** no need to reinstall or rewrite the existing 1664-MiB game partition to inspect the localized header; avoid operating on an actively written HDD image and preserve backup. Altering metadata requires a separate, checksum-preserving OSD resource injection workflow, not byte replacement in-place without verifying descriptor lengths.

### Known-good v4 cleanup package (2026-10-10)

User requested to **clean up the HDD restoration implementation**, explicitly deferring any HDD OSD icon/title translation. The user pasted the exact 212-line `.pnach` presently in use, confirming the same three working patch groups and known-good v1 driver-loader diagnostic header. Preserving runtime bytes and existing PCSX2 group names is deliberate: all **189 EE word writes** in the cleaned **`DC2_US_HDD_Restoration_Clean_20261010_v4.pnach`** are **identical** to the cold-boot verified v3 package and the exact user-pasted patch. Names `Existing HDD Title Menu Gate`, `EXPERIMENTAL HDD Driver Loader`, and `US HDD Installer 1664 MiB Capacity Test` retain PCSX2 saved enablement identity; the names are now explained as legacy compatibility labels, not indicators of an untested installer. No actual loader or capacity behavior changed.

Artifacts from **`DC2_US_HDD_Restoration_Cleanup_Repo_20261010_v4.patch`** are **committed directly on this branch** in **[`70948ffc24d9258888a73bc69cd3c42bd3983f7e`](https://github.com/Masamune3210/pcsx2/commit/70948ffc24d9258888a73bc69cd3c42bd3983f7e)** as one atomic Git tree/commit update. All five remote Git blobs were fetched after the ref update and matched the exact source blob SHAs:
- `patches/dark-cloud-2-us-hdd/DC2_US_HDD_Restoration_Clean_20261010_v4.pnach`
- `patches/dark-cloud-2-us-hdd/DC2_US_HDD_Loader_KnownGood_20261010_v4.S`
- `patches/dark-cloud-2-us-hdd/DC2_US_HDD_Restoration_Packager_20261010_v4.py`
- `patches/dark-cloud-2-us-hdd/DC2_US_HDD_Restoration_Tests_20261010_v4.py`
- `patches/dark-cloud-2-us-hdd/README.md`

Patch SHA-256: **`46d50ae80185d60c11d49ba4b25332aaea0f7f910fa2ff474ebccaf2ccb327e3`**. `git apply --check` succeeded in a new git repository, `git apply` succeeded, and all five resulting files matched the source artifacts byte-for-byte. The v4 .pnach also **rebuilt byte-for-byte** from MIPS .S using clang, ld.lld and llvm-objcopy, preserving loader SHA-256 `b8f2bf8a1a974c6bcc505ac9fbaafbc9f396a665628c47925354c80af4861ef1` (736 bytes). Running `--audit ...v4.pnach --verify-log 'Pasted text(20261010-211105).txt'` confirmed **189/189 exact known-good EE writes**. `python -m unittest ...v4.py` passed **11/11 tests**, including duplicate, wrong immediate, changed group name, unsupported/unknown directive, missing command and conflicting activation-log negative tests. The v4 packager explicitly rejects unrecognized/malformed `.pnach` directives instead of silently skipping them.

**Pending work:** Perform a **new v4 PCSX2 cold boot** (v4 executable entries same as v3, so this is packaging regression only); already recorded successful v3 gameplay remains authoritative. Validate memory-lifetime/soft-reset, no-HDD fallback, title return, and repeated cold boots before claiming hardware/universal safety. Do not reinstall the functional 1664-MiB partition just for cleanup. No OSD metadata localization changes.

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

## Historical initial resume plan (superseded by completed native HDD restoration above)

1. **Completed:** v3 diagnostic exported 27 decompiled functions per region. Next: run the read-only v4 instruction-byte audit at the verified US title HDD flag references.
2. **Completed at decompiler level:** the US gate is `TitleHDDCheckFlag = LanguageCode < 1`, read during initial HDD checks, title selection and rendering. Still verify exact raw machine instructions before patching.
3. **Partially complete:** these functions exist and largely match Japanese decompiled logic. Now resolve game-side DEV9/ATAD/HDD/PFS driver-load path and ensure English installer resources are present and usable.
4. Compare the **Japanese and US main ELF machine code and branches**, associated menu assets, file layouts, and module hashes. Symbol-address matching is complete, but implementation parity is unverified. Confirm any differences that matter to the installer and HDD-backed loading.
5. If supported by analysis, make the **smallest reversible game-side patch** on a separate experimental artifact and check for new *game-side* HDD IRX loads and PFS activity. A visible menu is only a first milestone; validate installation, loading and fallback independently.

### Safety / verification

- Work on **copies** of the PCSX2 HDD image and disc/ELF; both `UninstallApp` and installation code may mutate partitions.
- Avoid committing proprietary PS2 executable, IRX files, HDD image or Ghidra database to this public repository. Keep exported analysis reports locally.
- Don't conflate host DEV9 initialization, BIOS/OSD module loads and the game's own IOP environment.
- Historical pre-restoration note. Superseded: native US installation and HDD-backed gameplay are now user-verified. The verified v3 source, packager and .pnach have been provided as a tested four-file git patch, pending repository application.

### Local research artifacts from the original investigation

- `SCUS_972.13.gzf`: US Ghidra program archive.
- `DC2_US_HDD_Symbol_Reference_Map_20261010_v1.tsv`: exported stored-symbol/ref address map.
- `DC2_US_HDD_Decompile_Audit_20261010_v1.py`: Ghidra Jython read-only audit exporter.
- `Pasted text (3).txt`: complete stock-US PCSX2 boot log.
- `Pasted text (2).txt`: shorter boot/gameplay log containing HDD module lookup.
