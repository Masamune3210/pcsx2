# BIOS card handshake regression isolated from KELF (2026-10-17)

Branch: `note-working/magicgate-bios-card-auth-completion-20261017`, based on working `note-working/magicgate-authenticated-header-20261017`.

## Regression evidence

User bisected the "PocketStation®/1" display for every PS2 memory card to the sequence containing commits `a05107cfbbdd90f1b72b7a0002c4574a4cc049b2` (new standalone crypto header, no runtime dispatch changes) and `b23b1cb69feca5310cb8c99b74e3cb9310564ebd` (wires KELF auth into CDVD SCmd handling). First affected build: **PCSX2 v2.9.92-39-gb23b1cb69**, reproduced with HDD disabled, original BIOS `rom0:OSDSYS`, and `test2.ps2` recognized by PCSX2 as PS2 8 MB (unformatted). The source and log show:

```text
[4.5918] scmd=80 result=00
[4.5925] scmd=81 result=00
[4.5930] scmd=82 result=00
[4.5934] scmd=83 result=00
[4.6048] scmd=8F dataType=0 buffered=0x0 expected=0x0
[4.6051] scmd=8F result=80
```

This sequence happens before the BIOS attempts HDD initialization (later fails ATA -5 because HDD is disabled). It is unrelated to the authenticated HDD OSD KELF load.

The original SCmd 0x8F implementation returned success (`0x00`) when `mg_datatype=0`. The new implementation returns `0x80` when no KELF header had been authenticated. This was a category error: the older `0x80/0x81/.../0x8F` handshake is *not* a staged `0x90/0x92` KELF transfer. We have demonstrated the handshake response regression, not independently proven the BIOS's reason for rendering the PocketStation icon.

## Narrow fix

- `b99501a4a666aa503781caa906d874596663cebf`: track `legacyAuthCompletionPending` from 0x80/0x81; in 0x8F restore success only for this pending handshake when `mg_datatype==0`, `mg_size==0`, `mg_maxsize==0`, and none of `contentBusy`, `contentVerified`, `contentOutputReady` are set. Clear pending marker on completion.
- `1c62ce33da2bed5f3fbd1ed46eb131f07bef9f90`: clarify comment so PocketStation UI causality remains a hypothesis pending runtime testing.

No code in SIO0/SIO2, card image creation, CDVD KELF verification, or detailed MG logging was modified. This retains **legacy stub success** for a distinguished card handshake; it is **not** a real cryptographic card authentication implementation.

## Verification status

Code patch committed, not compiled or runtime-verified in this environment. Next test: BIOS-only cold boot with HDD disabled, exact same `test2.ps2` card and BIOS, using this branch. Confirm `[MGFLOW] RETURN scmd=8F result=00` in the original 0x80/0x81/0x82/0x83 sequence; check whether BIOS renders **Memory Card** instead of **PocketStation**. Then try a newly created 8 MB card format, on a disposable card image, and compare first-page write/erase behavior and repeated `(FileMcd) Warning: writing to uncleared data` diagnostics. If the UI/format symptoms persist, distinguish that from the handshake regression before another code change.

The successful authenticated HDD Utility install and existing KELF behavior on the parent branch should be independently rerun when practical; no protected-content verification bypasses were added.

## Previous abandoned approach

Sibling branch `note-working/sio2-ps2-pocketstation-probe-20261017` originally changed SIO2 PS1 command handling but reverted that speculative change. This was not evidence-based and should not be used as the fix. Sibling `note-working/sio-card-identification-format-trace-20261017` is an instrumentation-only experiment and is not needed to test the 0x8F regression. Preserve oververbose MagicGate logging while debugging.


## Memory-card authentication and mount sequence supplied by user

The observed protocol progression (not independently captured as SIO2 bytes by the earlier `emulog.txt`) is:

```text
81 11             Presence probe
81 F3             Driver authentication reset
81 F3             SECRMAN authentication reset
81 F7 01          Select authentication key mode
81 F0 00..14      Authentication exchange, interleaved
                  with MechaCon SCmd 80–88 and 8F
81 28             Get terminator
81 27 5A          Set terminator to 5A
81 26             Get card geometry
81 23 <page>      Select page to read
81 43 <length>    Read data chunks and spare/ECC bytes
81 81             End read
                  Validate superblock, backup metadata,
                  root directory, and mount filesystem
```

Source mapping: `pcsx2/SIO/SioTypes.h` defines PROBE=0x11, AUTH_F3=0xf3, AUTH_F7=0xf7, AUTH_XOR=0xf0, GET_TERMINATOR=0x28, SET_TERMINATOR=0x27, GET_SPECS=0x26, SET_READ_SECTOR=0x23, READ_DATA=0x43, READ_WRITE_END=0x81. Dispatch in `Sio2.cpp` matches the list. `MemoryCardProtocol::AuthXor` handles fixed-length framed/XOR responses for submodes, not full cryptographic challenge-response; `AuthF3` resets the terminator to READY for a present card; `AuthF7` returns a framing terminator. The two F3 calls are separate points in the protocol, not distinct SIO command codes.

Thus the BIOS's empty-data SCmd `0x8F` belongs to **memory-card authentication orchestration**, and should not be processed as an incomplete `0x92` KELF content transfer. The legacy completion reply is a compatibility placeholder pending actual emulation of the card/MechaCon exchange and should not be described as verified card authentication.

Expected next stage after the corrected `0x8F`: card terminator/geometry, page reads, and filesystem validation; compare against same-build pre-regression baseline if possible. The UI's PocketStation fallback is correlated with `0x8F` rejection but not yet independently traced to the exact BIOS decision. Formatting errors involving first-page writes may be downstream of the incomplete exchange or a separate fault; test rather than assuming.

