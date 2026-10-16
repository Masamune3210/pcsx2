# HDD Utility MagicGate: Codex native MechaCon crosswalk (2026-10-16, v23)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Continue from `docs/hdd-utility-ps3-gxemu-dispatch-state-20261016-v22.md`. DNAS remains parked. No PCSX2 source change or hardware implementation has been claimed.

## Separate new evidence repository

User supplied `Masamune3210/ps2mgdecomp`, private repository (default branch `master`), developed in parallel via Codex. Read `README.md`, `CHECKPOINT.md`, `reports/2026-10-16-mg-kelf-investigation.md` and focused `decompiled/Dragon.c`; project still points to local `../PS2.gpr`. **This is the Dragon MechaCon 6.12 ARMv4T firmware, NOT a pre-Dragon 3.06 dump from the user's SCPH-39001.** Its output includes 93 focused C/native exports, 90 annotated functions over 8 Ghidra programs, 36 recovered function starts (Dragon 895->928 functions, 64355->66055 instructions), raw byte preservation and independent verification. Static only: not real hardware/keys/test vectors or software cipher implementation.

Report URL:
https://github.com/Masamune3210/ps2mgdecomp/blob/master/reports/2026-10-16-mg-kelf-investigation.md

Focus in Dragon:
- Task4 `0x1353C` dispatches SCmd `0x80..0x9F` through 32-entry table `0x38034`.
- Task9 `0x00000BA8 ProcessMagicGateWork` handles authentication, KELF header and payload work.
- `0x000010E0 VerifyAndDecryptKelfHeader`: header authentication (before parsing), policy/region/revocation, Kbit/Kc unwrapping, BIT decryption/authentication and block descriptors. Relevant original bytes are in `native/Dragon.txt`; recovered C in `decompiled/Dragon.c`.
- `0x00001A5C DecryptAndAuthenticateKelfBlocks`: payload block processing.
- `0x00001FBC` staged 8-byte transforms; incoming r4 cursor invariant unresolved. Do not invent a zero/extra signature parameter.
- Hardware cipher MMIO base `0x03004100`: selectors `0,1,0x10,0x11`; eight-byte key/input/output, chaining. Consistent with DES-family design, but firmware does not include software implementation and hardware semantics are not proven with oracle.
- `0x00002D4C PollMagicGateWorker` has proven relative Thumb switch. Ghidra needed bounded disassembly/false-string repair and explicit jump override, not new firmware bytes.

## Crucial command-sequence crosswalk

Actual official HDD Utility `SCUS-97395` trace on PCSX2:
```
SCmd90 00 A0 01 00 00
26 * SCmd8D with 16 bytes each (0x1A0 total)
SCmd8F then error/BIT_FORMAT_REJECT in existing PCSX2 stub
```

Native Dragon independently confirms:
- `0x2784 BeginKelfHeader`: exact 5 bytes = `mode:u8, length:LE16, sourceSlot:u8, destSlot:u8`. `00 A0 01 00 00` means mode0, length 0x1A0, source0, dest0; if mode0 and valid length, firmware accepts state `0x0B` **without requiring authenticated card slots**. Other nonzero modes require authenticated slots. This is NOT bypassing cryptography: the worker still authenticates and transforms the KELF.
- `0x25BC AppendMagicGatePayload`: accumulates <=0x800 bytes; in state0x0B once count >= declared length, moves to state0x0C and schedules Task9.
- `0x2D4C PollMagicGateWorker`: when state0x0C, status0x10C produces busy=1; status0x20C completes and advances state0x0D, returns0; status0x300 reports error_detail and resets state1. This operation polls background authenticating/decrypting; it is NOT itself the cipher.
- `0x284C BeginKelfBitTableRead`: only valid in state0x0D; exposes BIT length/backing buffer and enters state0x0E.
- `0x26B0 ReadMagicGatePayload`: streams at most 16 bytes per read when states 0x0E/0x12/0x1E.

Important distinction: **do not read the encrypted BIT as plaintext when returning SCmd8F**. Complete header arrives correctly already; missing genuine worker behavior causes rejection. The SCmd8F command response alone cannot replace hardware transformations.

## Comparison to parallel PS3 ps2_gxemu investigation

PS3 emulator `ps2_gxemu.elf` Ghidra work has identified virtual UART port10, 24-byte BE64 function descriptors with `r2=0x678548`, TX/RX framed transport, `DispatchVirtualUartPacketTransfers@0x250D70` and dispatch states 0-4. Those are *transport/controller-side* traces. The Dragon reconstruction is a *native firmware-side* processing model. Do not map PS3 UART outer frame class byte 0x90 to native MechaCon SCmd90 without packet proof. PS3 may implement a pre-Dragon virtual device not identical to Dragon 6.12, so compare semantics using actual commands and payloads, not addresses/keys.

## Best next steps, deliberately narrow

1. Use native `BeginKelfHeader`/`AppendMagicGatePayload`/`PollMagicGateWorker` to specify PCSX2 state transitions, log scheduling of KELF processing, and remove **the erroneous assumption** that ciphertext can be validated as plaintext. Do not return hardcoded success or suppress checks.
2. Map `VerifyAndDecryptKelfHeader` input/output and first 0x20+16*N authenticated record bytes; isolate the precise decrypt/auth boundary. Recover the 8-byte hardware engine modes/keys using a separate real hardware or known-answer oracle rather than naming modes as DES encrypt/decrypt by guess.
3. Compare native Dragon 6.12 and PS3 emulated 3.09 behavior for common public-content decryption, keeping SCPH-39001 pre-Dragon 3.06 differences explicit.
4. Continue Ghidra PS3 dispatch state producer tracing independently; no need to rediscover transport wrapper names already applied.

No Ghidra script generated in this crosswalk because original Codex task has already applied its saved project annotations and verified fresh reopen; it warns against replaying its apply. Directly usable metadata and full commands are in `ps2mgdecomp/function-index.json` and the native/decompiled folders. New scopes need new baselines. Do not change that completed project opportunistically.
