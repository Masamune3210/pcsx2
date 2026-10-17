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
