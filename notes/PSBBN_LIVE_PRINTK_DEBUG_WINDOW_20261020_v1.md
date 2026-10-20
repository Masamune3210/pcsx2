# PSBBN live Linux printk viewer (host-only, run12-validated ring)

## Corrected producer cursor (source-checked 2026-10-20)

A Sony PS2 Linux 2.4.17 source reference [kernel/printk.c](https://github.com/rickgaiser/linux-2.4.17-ps2/blob/master/kernel/printk.c) declares `static char log_buf[16384]`, then `unsigned long log_start`, `con_start`, `log_end` and `logged_chars` (each 4 bytes on PS2 MIPS). In raw snapshots #004, #020 and #038, the three neighboring words at physical 0x001FC4EC, 0x001FC4F0, and 0x001FC4F4 are respectively all 3511 then all 3607, so **snapshots alone could not distinguish the console-reader cursor from the write cursor**. Source-based layout identifies the actual producer `log_end` at **0x001FC4F4**. The observer now reads that, not 0x001FC4F0, because console output need not track printk writes when no guest console drains it. This historical source is a close reference, not authenticated byte-identical Sony BB Navigator 0.32 source; confirm these data symbol names/addresses against the native ELF table when possible. No guest changes or compilation performed.


**Status:** PCSX2 Qt implementation committed; **not compiled or emulator-tested** per user's no-compilation instruction. No guest executable, kernel, HDD image, framebuffer, savestate, or ATA protocol semantics changed.

## How to use

1. In the Qt UI, open **Tools → PSBBN Linux Kernel Log...** before booting BB Navigator (or at any point while it runs).
2. A **separate, read-only window** titled `PSBBN Linux Kernel Log (read-only)` opens. It samples the Linux ring at 100 ms intervals from EE RAM on the CPU thread and sends copies to the UI thread. It does not patch or invoke code in the guest.
3. The same messages are written automatically to **`<PCSX2 logs folder>/psbbn-kernel-log.txt`**, independent of `emulog.txt`. The viewer's Log menu can open the log folder and clear the displayed text. File logging appends with an observer-session date header.
4. Attach after boot to replay ring content still resident in guest memory, marked `[replay]`. Live lines include the PCSX2 host message-time prefix (`[ 123.4567]` style). These are **observation times**, not timestamps emitted by the PS2 kernel. The kernel's original `<4>` priority codes and message text are preserved.
5. Closing the separate window stops sampling and file writes. Reopening it catches up from the currently available ring. The observer is opt-in, does not require the ordinary emulator Log Window to be enabled, and has no effect on the screen shown by PSBBN.

## Verified kernel ring mapping from the user's run12 `.p2s` EE RAM

Image: SCPN-60160 PlayStation BB Navigator 0.32, Sony Linux `2.4.17_mvl21`. These addresses **must not be assumed correct for arbitrary Linux/PS2 images**.

```text
Ghidra label/redeclaration:
0x801F84E8 -> PSBBNPrintkLogBuffer
0x801F84E8 - 0x801FC4E7
Type: char[0x4000]          # 16 KiB circular printk text

0x801FC4F0 -> con_start
Type: uint32_t             # console-reader cursor, NOT printk write index

Adjacent PS2 Linux 2.4.17 source-derived candidate globals (verify against native ELF before mass renames):
0x801FC4EC -> log_start
Type: uint32_t
0x801FC4F4 -> log_end
Type: uint32_t             # actual printk producer write cursor

0x801FC4F8 -> logged_chars
Type: uint32_t             # printk character count
```

The lower addresses are **physical EE offsets** with the KSEG0 `0x80000000` prefix omitted (`0x001F84E8` and `0x001FC4F0`). Witness text at buffer start: `<4>PlayStation 2 SIF BIOS:`.

Direct offline read-only checks against extracted `eeMemory.bin` images:

| Savestate | Ring end cursor | Newly decoded bytes | New DMA timeout messages |
| --- | ---: | ---: | ---: |
| Run12 #004 | 3511 | 3511 | 0 |
| Run12 #020 | 3607 | 96 | 3 |
| Run12 #038 | 3607 | 0 | 0 |

The 96-byte delta is **exactly three** `<4>hda: timeout waiting for DMA\n` records. The printk buffer's 16 KiB extent follows from the 0x4000-byte gap between buffer start and the immediately following cursor globals; runtime dumping additionally confirms the cursor points to the first unwritten byte. These checks support the chosen offset and ring-decoder behavior, but **do not substitute for execution of PCSX2**.

## Changed production files

- `pcsx2-qt/LogWindow.cpp`: opt-in standalone Qt debug window, read-only EE RAM snapshot, version witness, ring cursor/tail, wrap/drop detection, UI text and dedicated file sink; no separate Qt-MOC unit needed.
- `pcsx2-qt/LogWindow.h`: two UI entry points.
- `pcsx2-qt/MainWindow.cpp`: Tools menu entry and teardown.

The work leaves `pcsx2/DEV9/**`, EE interpreter/JIT, VM state saving/loading, and everything inside the guest unchanged. HDD savestates remain **fully permitted**, consistent with the user's explicit reversal of the earlier ban. No new UI defaults or per-game emulation settings are imposed.

## Limits and next verification

- The address and initial-message fingerprint are **specific to the BB Navigator guest kernel version**. Before the ring is recognized, no guest memory content is displayed or logged. Reboot or savestate restore causes cursor resynchronization and replay of still-available kernel messages. After recognition, continued observation uses the existing ring even if the oldest bytes wrap.
- The guest's `printk` output is not a full trace. There can be periods with no new messages while EE/IOP threads run, so correlate with `emulog.txt`, HDD command/IRQ diagnostic traces and savestates.
- Sampling executes on the CPU thread, and UI updates/file writes execute only on the UI thread; there are no guest writes or arbitrary virtual-memory translation side effects.
- If more than 16 KiB arrives between polls, the observer warns of overwritten messages and reads only the latest 16 KiB. Very long unterminated lines are bounded to one ring's storage.
- **Compilation and runtime are intentionally not verified.** When user authorizes it, build the Windows Qt target with Visual Studio and cold boot the guest. Confirm separate window opens before boot, #004-style boot lines display, a deliberate 30-second stall yields actual host-time gaps in the log, guest recovery adds relevant kernel records, shutdown closes normally, and savestates still work.
- If a source/build issue appears, fix only this observer first, not the unrelated DEV9 DMA or TLB paths.

Relevant prior mission: [Run12 DEV9 DMA timeout](PSBBN_RUN12_DEV9_DMA_TIMEOUT_MISSION_20261020_v1.md); [DMA FIFO source finding](PSBBN_RUN12_SPEED_FIFO_DEADLOCK_FINDING_20261020_v1.md).
