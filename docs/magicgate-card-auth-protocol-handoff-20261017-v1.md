# PS2 MagicGate memory-card authentication: protocol-first investigation (2026-10-17)

Branch: `note-working/magicgate-card-auth-protocol-20261017`, from known HDD Utility-working `note-working/magicgate-authenticated-header-20261017`. This branch is for implementing **actual card ↔ MechaCon authentication state**, not restoring a blanket success response.

## Confirmed regression, with scope

User bisected PocketStation UI misidentification to `b23b1cb69feca5310cb8c99b74e3cb9310564ebd` (introduced KELF-only 0x8F gating in `pcsx2/CDVD/CDVD.cpp`). `a05107cfbbdd90f1b72b7a0002c4574a4cc049b2` adds a crypto header but doesn't change runtime dispatch. First failing log: PCSX2 `v2.9.92-39-gb23b1cb69`, BIOS 1.60 US, original `rom0:OSDSYS`, **HDD disabled**. During `SecrAuthCard` initiation `0x80=00`, `0x81=00`, `0x82=00`, `0x83=00`, and `0x8F=80` with no staged KELF data. This fails *before* HDD initialization. Later builds reproduce PocketStation icons for all PS2 cards, failure of DVD Player card detection, and some card-format writes to uncleared flash at address 0. No direct proof yet all latter symptoms share exactly the same cause.

The old PCSX2 0x8F success for `mg_datatype == 0` was permissive and did not cryptographically authenticate anything. The attempted one-shot workaround on sibling `note-working/magicgate-bios-card-auth-completion-20261017` was **reverted** in `de32f1fcbba5c35859827be205db2e17f940d3b8`. Do not reinstate it: genuine card authentication needs **two distinct calculations and polls**, not a single unconditional completion.

## Primary source: ps2dev reconstruction

- [MCMAN SIO2 transport](https://github.com/ps2dev/ps2sdk/blob/master/iop/memorycard/mcman/src/mcsio2.c): `mcman_initPS2com` registers `SecrSetMcCommandHandler(secrman_mc_command)` and the McDevID callback; `secrman_mc_command` forwards SIO2 transfers. `mcman_probePS2Card` invokes `SecrAuthCard` after card probe/reset; returns `sceMcResFailAuth` on failure, **before terminator/geometry/file-system access**.
- [SECRMAN orchestration](https://github.com/ps2dev/ps2sdk/blob/master/iop/security/secrman/src/secrman.c): `SecrAuthCard` coordinates card and MechaCon challenge-response. `SecrResetAuthCard` resets card+MechaCon.
- [SECRMAN MechaCon calls](https://github.com/ps2dev/ps2sdk/blob/master/iop/security/secrman/src/MechaAuth.c): `pol_cal_cmplt()` sends repeated SCmd 0x8F, loops while result byte==1, returns success only when final byte==0 (command-send failure is also failure); it is called **twice** in `SecrAuthCard`.
- [SECRMAN card framing](https://github.com/ps2dev/ps2sdk/blob/master/iop/security/secrman/src/CardAuth.c): F0 reads/writes require proper SIO2 framing, terminator, and checksums; card output bytes are reversed into the 8-byte buffers. A pass on SIO2 transport framing alone is not cryptographic authentication.

### Verified stage order from `SecrAuthCard`

```text
81 F3         reset card auth; SCmd 80(cnum); 81 F7 01 select card key mode
81 F0 00      begin exchange; SCmd 81(cnum)
81 F0 01/02   read CardIV and CardMaterial; SCmd 82(IV + material)
81 F0 03/04   read CardNonce; SCmd 83(nonce)
81 F0 05      stage marker; SCmd 8F (poll until !=busy)    PHASE 1
SCmd 84/85    read MechaCon challenge material
81 F0 06/07   write challenge material to card
81 F0 08..0E  card challenge exchange
81 F0 0F/11   read CardResponse1/2; SCmd 86(responses)
81 F0 12/13   read CardResponse3; SCmd 87(response3)
SCmd 8F       poll calculation until !=busy              PHASE 2
81 F0 14      finalize card exchange; SCmd 88 complete
81 28         get terminator; 81 27 5A set terminator
81 26         card geometry; 81 23/43/81 read raw pages and ECC;
              filesystem mount/validation follows
```

`0x8F` has one-byte status semantics: `01` busy, `00` calculation successfully completed, other codes failure. This is distinct from 0x8F when a 0x90/0x8D KELF header or 0x92/0x8D encrypted-content transfer was staged. Do not use `mg_datatype == 0` alone to identify the card mode; it is also used for KELF content, so track state from actual command sequence.

## Present emulator gaps (as examined on parent branch)

- `pcsx2/CDVD/CDVD.cpp` 0x80/81/82/83 handlers currently always respond 00; 0x84/85 emit fixed challenge bytes; 0x86/87 likewise always respond 00; 0x88 returns 00; 0x8F handles only KELF header or KELF content and returns 80 for this card path. None prove challenge-response validity. This is a broader stubbed authentication pipeline, not just one wrong result byte.
- `pcsx2/SIO/Memcard/MemoryCardProtocol.cpp` `AuthXor()` processes F0 submode framing/XOR, `AuthF3()` resets ready terminator, `AuthF7()` returns a terminator. These are **byte-level protocol stubs**, not card cryptographic state/response generation.
- `pcsx2/SIO/Sio2.cpp`, `Sio0.cpp`, and `MemoryCardProtocol.cpp` were byte-identical between fork and current upstream during prior comparison; do not alter memory-card identification blindly.
- Additional supplied `.mgkeys` fields such as `MG_CARDKEY_0` are now ignored by the KELF parser; their meaning/provenance/correct cryptographic use **remain unestablished**. Do not wire them up by guessing algorithms or leak key bytes into logs.
- The prior stub-compatibility proposal (return 00 without any calculation) is insufficient for correct certification detection and may hide failures. If a user requests a temporary compatibility bypass, it must be opt-in and clearly distinguished from authenticated status; default goal is real protocol validity.

## Research and implementation next steps

1. Use the ps2dev SECRMAN reconstruction and the user's existing Ghidra SECRMAN decomp, where needed, to identify card↔MechaCon state transitions and each F0 submode's input/output contract. Confirm against correct firmware variant and avoid blindly trusting a decomp if inconsistent.
2. Obtain reference observations for known genuine MagicGate 8 MB card vs non-certified/PS1 card (per phase card and MechaCon command/result bytes, without logging or committing secret keys). Determine whether each supplied card key is sufficient for emulation and independently validate algorithms before implementation.
3. Introduce separate per-port/slot card auth state and MechaCon auth worker, with two 0x8F completion phases and proper busy/failure; avoid cross-session contamination, ensure stage guards/length limits (historic 0x8F buffer safety), and preserve KELF auth untouched.
4. Verify genuine card: BIOS shows memory card with MG capability, geometry and filesystem mount succeed. Verify bad/non-MG card fails correctly, wrong challenge does not certify, and stage state resets. Verify official HDD Utility and installed HDD OSDSYS still boot.
5. Card-format writes and DVD Player detection should be re-evaluated only after valid card authentication flow, using the same image and BIOS conditions.

## Validation and change state

This branch currently contains only this handoff document; **no emulator code changes, no claim of cryptographic verification, and no build/runtime verification**. Keep existing verbose `MGFLOW`/`MGRAW` logging intact as expressly requested. The sibling SIO2-response branch was abandoned/reverted and must not be revived on the strength of PocketStation icons alone.

## Confirmed fallback path explains the two downstream symptoms

The ps2dev reconstruction explicitly implements PS2-to-PS1/PDA **card-type fallback** in `iop/memorycard/mcman/src/main.c` (`McDetectCard2` / `mcman_detectcard`):

- First try `mcman_probePS2Card` (or `mcman_probePS2Card2`).
- On result `r < -9`, try `mcman_probePS1Card` (or `...2`).
- If the PS1 probe returns `r >= -9`, `mcman_probePDACard` selects `sceMcTypePS1` for a nonzero result, or `sceMcTypePDA` (PocketStation) for zero.
- `iop/memorycard/mcman/src/mcsio2.c::mcman_probePS2Card` returns `sceMcResFailAuth` if `SecrAuthCard` fails, which is defined as **-90** by `common/include/libmc-common.h`; therefore it directly satisfies the fallback threshold `r < -9`.
- `McFormat` checks MCMAN's **detected cardtype**, dispatching `mcman_format2` only for `sceMcTypePS2`, and `mcman_format1` otherwise.
- `iop/memorycard/mcman/src/ps1mc_fio.c::mcman_format1` uses **128-byte PS1/PDA page buffers** and `McWritePS1PDACard`, writing directory/bad-block records and finally page **0** with PS1 magic `MC`. This is consistent with 128-warning flash-write bursts at address zero in the user's affected build and could overwrite page zero of the configured PS2 card image, because PCSX2's `MemoryCardProtocol::PS1Write` is backed by the same card data and SIO2 does not discriminate PS1 command dispatch by physical card type.

**Inference boundary:** The code establishes the causal control-flow path; the previously supplied PCSX2 logs do not include actual SIO2 per-command traces to show that each PS1 write occurred in that run. The exact source-version match to retail BIOS MCMAN needs confirmation, although the observed symptoms are consistent. Card file creation remains identical across the fork/upstream and is not the supported cause. Fix the upstream authentication cause, not PS1/PDA fallback or card formatting, then verify with a disposable card image.

Primary source links:
- https://github.com/ps2dev/ps2sdk/blob/master/iop/memorycard/mcman/src/main.c
- https://github.com/ps2dev/ps2sdk/blob/master/iop/memorycard/mcman/src/mcsio2.c
- https://github.com/ps2dev/ps2sdk/blob/master/iop/memorycard/mcman/src/ps1mc_fio.c
- https://github.com/ps2dev/ps2sdk/blob/master/common/include/libmc-common.h
