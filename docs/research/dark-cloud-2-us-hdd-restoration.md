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

### Selected US Ghidra addresses (to verify in the actual program)

| Address | Symbol |
| --- | --- |
| `0019F1F0` | `TitleInit` |
| `001A3630` | `TitleHDDInstallInit` |
| `001A3A80` | `TitleHDDInstallKey` |
| `001A48F0` | `TitleHDDInstallDraw` |
| `001A4D00` | `CheckAppInstallForTitle` |
| `0021B110` | `HDDMenuLoop` |
| `0021BD50` | `MountHDDFileSystem` |
| `0021BFF0` | `CreateInstallThread` |
| `0021C270` | `InstallLoop` |
| `00048650` | `GetMainFileDev` |
| `00048660` | `ChangeHddFile` |
| `00276950` | `TitleHDDCheckFlag` (global) |
| `00276518` | `DefaultFileDev` (global) |

Addresses and stored reference locations were recovered from the supplied `SCUS_972.13.gzf` Ghidra program, not dynamically traced.

## Unmodified US PCSX2 boot baseline

Source: complete PCSX2 boot log from the investigation, `Pasted text (3).txt` (3,263 lines), starting at BIOS boot with HDD-enabled Browser 2.00 installed.

- PCSX2 host DEV9 exposes a 40 GiB HDD image. Early BIOS HDD loader successfully reads `pfs0:/osd100/hosdsys.elf`; Browser 2.00 later initializes its own DEV9/ATAD/HDD/PFS modules. **These HDD reads and module loads are from system software, not Dark Cloud 2.**
- The system software reboots/replaces the IOP environment multiple times during disc handoff. Do not assume a module loaded under Browser 2.00 remains registered when the game starts.
- At approximately `99.6358` through `99.9017` in this capture, *Dark Cloud 2* loads `SIO2MAN`, `PADMAN`, `MCMAN`, `MCSERV`, `LIBSD`, `SDRDRV`, `MODMIDI`, `MODHSYN`, `EZMIDI`, `EZBGM` from its disc. No game-side DEV9/ATAD/HDD/PFS module loads appear in the observed startup.
- An earlier partial gameplay log showed `searchbyname: module name hdd` and `return -202`. This establishes a failed module lookup at that point, **not** that the game lacks HDD code or necessarily initiated the lookup itself.
- Fast CDVD was enabled and a widescreen memory patch was present in the captured emulator configuration. Disable timing-altering options for controlled tests, and record any active patches.

## Japanese reference imported into Ghidra (2026-10-10)

The Japanese main executable `SCPS_150.33` has now been analyzed in **Ghidra 12.1.3** using `r5900:LE:32:default (1.4)`, image base `0x100000`. The Japanese `.gzf` archive has now been supplied and its stored HDD symbols extracted. **This is a symbol/address comparison, not a verified machine-code or behavioral comparison.**

- Japanese ELF MD5: `483996783678ccf04ead7eeff6c51e66`
- Japanese ELF SHA-256: `05985e62f09e9a2fb6d818323f422932b78c6dc1bc41d3757f18cade7451aa06`
- Japanese ELF comment: `MW MIPS C Compiler (2.4.1.01)`, `PlayStation2` (not verified against the US ELF comments)
- JP analysis: **7,748 functions**, **540,730 instructions**, **32,419 symbols**, **39,740 defined data items**, **249 datatypes**, **114,056,425 indexed bytes**.
- Earlier US analysis: **7,809 functions**, **548,645 instructions**, **32,768 symbols**, **44,691 defined data items**, **251 datatypes**, **114,173,295 indexed bytes**.
- Differences are analysis counts, **not proof of removed or added code**. Both programs use the same Ghidra processor language and original image base, easing decompilation/structural matching.

## Japanese vs US HDD symbol comparison (2026-10-10)

Analyzed the embedded Java-serialization/GZIP ZIP payload from `SCPS_150.33(1).gzf` without requiring Ghidra at extraction time; the first `FOLDER_ITEM` stream is a raw-deflate local ZIP entry, and symbol records store names and address metadata. **37 matching HDD-related function/global symbols** were mapped to the earlier US reference list; no function byte-for-byte comparison or branch-condition comparison has yet been performed.

| JP address | US address | Named symbol |
| --- | --- | --- |
| `0019E260` | `001A3630` | `TitleHDDInstallInit` |
| `0019E6E0` | `001A3A80` | `TitleHDDInstallKey` |
| `0019F550` | `001A48F0` | `TitleHDDInstallDraw` |
| `0019F910` | `001A4D00` | `CheckAppInstallForTitle` |
| `0019F950` | `001A4D40` | `CheckHDDInstall` |
| `00214AD0` | `0021B0C0` | `InitHDDMenu` |
| `00214B20` | `0021B110` | `HDDMenuLoop` |
| `00215450` | `0021BA40` | `HddConectCheck` |
| `002155D0` | `0021BBC0` | `CheckAppInstall` |
| `00215760` | `0021BD50` | `MountHDDFileSystem` |
| `00215A00` | `0021BFF0` | `CreateInstallThread` |
| `00215C80` | `0021C270` | `InstallLoop` |
| `0026D418` | `00276518` | `DefaultFileDev` |
| `0026EE10` | `00277F28` | `HDDModeSelect` |
| `0026F748` | `00278890` | `AppInstall` |

An especially useful alignment: `InitHDDMenu` through `InstallLoop` and the intervening installer routines all have a **constant US-JP address delta of `+0x65F0`**. The title-screen handlers have different deltas around `+0x53A0` to `+0x53F0`, so **do not globally rebase one executable into the other**.

The Japanese archive includes `HDDPhase` at `0026EDE4`, `HDDConfirmType` at `0026EDE8`, `HddConnect` at `0026F744`, and `HDDImage` at `01DEF7B0`. The US archive's `TitleHDDCheckFlag` at `00276950` has **no literal symbol-name match in the JP archive**. This does not establish whether the actual corresponding variable or logic exists under a different name.

### Ready for the next comparison

- Locally generated `DC2_JP_US_HDD_Address_Comparison_20261010_v2.tsv`: 38 recovered address mappings.
- Locally generated `DC2_HDD_CrossRegion_AnnotateAndExport_20261010_v2.java`: standalone Ghidra Java script for Ghidra 12.1.3; run against each archive to check matching symbols, add non-destructive cross-region comments, and export incoming references and decompilations. **Not yet tested within Ghidra.**
- Next: compare `TitleInit`, `TitleModeKey`, `TitleModeDraw`, the title case dispatchers, and especially US reads/writes of `TitleHDDCheckFlag`. Compare JP and US decompiler output, then examine region checks and possible menu accessibility; do not patch until actual conditions are resolved.




## BSim propagation baseline (2026-10-10)

BSim propagation was run **between the Japanese and US Ghidra databases in both directions**. **No additional labels were created in either database**, per the observed result. Match counts, confidence/similarity thresholds, ignored/conflicting matches, and the exact set of correspondences have not yet been exported or verified. Do not infer that no functions matched or that the binaries are structurally identical: both databases already contain extensive Metrowerks-mangled function symbols. Next targeted investigation remains the decompiled title/HDD conditional checks rather than another broad propagation pass.


## JP/US Ghidra v2 audit failure (2026-10-10)

Both `DC2_JP_HDD_CrossRegion_Audit_20261010_v2.txt` and `DC2_US_HDD_CrossRegion_Audit_20261010_v2.txt` report **matched=0, mismatches=37, comments_checked=0**; their additional JP globals and the US `TitleHDDCheckFlag` are also mismatched. **No decompilation or executable comparison occurred; no database changes were made.** The v2 script's `getPrimarySymbol(addr)`/exact mangled-name check is insufficient to distinguish non-primary symbols, displayed/demangled names, and incorrect addresses.

**Previous JP/US address mappings are provisional and must not be applied as trusted ground truth until independently verified in live Ghidra.** A new **read-only** `DC2_HDD_CrossRegion_DiagnoseAndExport_20261010_v3.java` script was prepared locally (not Ghidra-tested). It checks every symbol at each claimed address, the function at/containing the address, memory mapping, candidates found by symbol name, and exports references/decompilations for independently resolved matches. Any unresolved/ambiguous states are listed at the end. Run it once in JP and once in US, then use its actual-address reports to correct the cross-region map and inspect title-screen gating. Do not repeat BSim propagation or patch any address based only on v2 maps.


## Resume plan (smallest useful next steps)

1. Run the **read-only** Ghidra audit script from this investigation (`DC2_US_HDD_Decompile_Audit_20261010_v1.py`) on the supplied US `SCUS_972.13.gzf` and retain the full decompilation/reference report. The script was prepared but **not verified by execution inside Ghidra**.
2. Audit `TitleInit`, `TitleModeKey`, `TitleModeDraw`, title dispatcher cases, and **every write/read of `TitleHDDCheckFlag`**. Identify the precise condition controlling the HDD menu and any regional switch.
3. Trace `TitleHDDInstallInit/Key/Draw` through `HddConectCheck`, `CheckAppInstall`, `CheckInstallSpace`, `CreateInstallThread`, `InstallLoop`, mount/unmount and asset-source switches. Check for stubs and failure paths.
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
