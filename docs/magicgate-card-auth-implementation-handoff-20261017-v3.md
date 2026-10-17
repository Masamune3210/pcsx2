# MagicGate card-auth implementation status (2026-10-17, v3)

Branch: `note-working/magicgate-card-auth-protocol-20261017` in `Masamune3210/pcsx2`.
Supersedes `docs/magicgate-card-auth-implementation-handoff-20261017-v2.md` and inherits its reference mapping, state-machine design, testing checklist, and caveats.

## Implemented

- Runtime authentication across **SIO2 card F3/F7/F0 stages** and **MechaCon SCmd 80–88/8F**, in `pcsx2/SIO/Memcard/MemoryCardProtocol.cpp`, `pcsx2/CDVD/CDVD.cpp` and newly added `pcsx2/CDVD/MagicGateCardAuth.h`.
- External retail card-key parsing in `pcsx2/CDVD/MagicGateCrypto.h`: `MG_CARDIV_0`, `MG_CARDKEY_0`, `MG_CARDIV2_0`, `MG_CARDKEY2_0`, `MG_CHALLENGE_IV`. The other extra `*_1`/`*_2` names remain intentionally ignored rather than substituted for retail CEX keys. The user trace previously confirmed all five required names appear in their own .mgkeys (values neither recorded nor committed).
- Standard two-key 3DES CBC for card-specific unique-key derivation, generating 3 chained challenges, independent card-side challenge checking, and MechaCon response verification. Both 0x8F polls return **busy 01**, then **verified 00** on correct input, not placeholder success; invalid second response returns **84**, state/order errors **80**. Completing SCmd 88 requires both sides to finish.
- Two independently modeled participants, using synthetic per-slot card identity/nonces for the emulator's virtual card; not tied to a particular real card serial or a hardware RNG. Reject unavailable key material, unsupported key mode and tampered challenges, without exposing key bytes in logs. No changes to flash/PS1 fallback code; original MGRAW/MGFLOW output remains deliberately verbose.
- Standalone C++20 regression `tools/magicgate/SmokeMagicGateCardAuth_20261017_v1.cpp` and GitHub Actions workflow compilation with `-Wall -Wextra -Werror`.

## Additions since v2

- `d40e1f4f` adds `ParseKeys(..., requireKelfKeys)` with the default **true**, preserving all existing KELF call-site requirements. This allows a separate card-only parse when false.
- `863f66b7` passes `cardOnly=true` when SCmd 80 loads card keys, so a BIOS-adjacent .mgkeys containing **only** the five card keys can perform card authentication. Subsequent KELF header/content verification still requires all nine original KELF keys.
- `e7a3058a`, `5d2a4d90`, `0dca5180` add parser regression cases covering complete retail keys, partial card sets, ignored unrelated keysets, and card-only files (and correct fixture delimiters).

## Source and verification

- ps2dev `iop/security/secrman/src/{secrman.c,CardAuth.c,MechaAuth.c}`, `iop/memorycard/mcman/src/{main.c,mcsio2.c,ps1mc_fio.c}`; ps3mca_tool `src/mecha_emu.c`, `src/config.c`; all at public GitHub URLs already recorded in v2.
- The authentic BIOS-only failing log at `b23b1cb69` shows 0x80/81/82/83 success and 0x8F failure before HDD init. A PS2-auth-failure result (-90) satisfies the MCMAN PS1/PDA fallback threshold (< -9), explaining why a virtual PS2 card can be interpreted as PocketStation and then formatted with the 128-byte PS1 writer.

## What remains unverified / next step

The emulator branch commits and standalone CI workflow exist, but **no successful build or CI run status was retrievable from the GitHub connector**, and **no post-implementation BIOS execution log or screen** has been supplied. Do not claim the runtime defect is fixed yet. The first critical check is cold-boot with HDD disabled and a disposable 8 MB PS2 card: confirm card-auth 0x8F runs twice, card recognized correctly, formatting follows the PS2 path, and no first-page flash warning repeats. Then test previously working authenticated HDD utility and HDD-enabled Browser. User explicitly forbade trimming detailed logging.

Potential follow-up work: validate per-stage SIO2 framing and exact retail BIOS expectations; support other MagicGate key modes if needed; consider per-card identity persistence/savestates; F1/F2 card-binding remains out of scope, as it was before.

Avoid the withdrawn one-shot 0x8F success workaround on `note-working/magicgate-bios-card-auth-completion-20261017`. It was reverted for being cryptographically incorrect.

## Compile correction: SIO2 card slot access (2026-10-17)

A Windows/MSVC build surfaced six diagnostics in `pcsx2/SIO/Memcard/MemoryCardProtocol.cpp`: `C2065 g_MultitapArr` (three occurrences), `C2737 slot` uninitialized, and `C2660 Runtime::ResetCard/SelectCardKey` apparent argument-count mismatches. All come from using `g_MultitapArr` without the multitap header in the protocol implementation, and its dependent initializer/call-expression errors. No change to MagicGate calculations was needed.

**Commit `bee3eef237b71a5623b5018f7da8b750d5736670`** resolves the selected card slot directly from `mcd->port` and `mcd->slot`. `Sio2::Memcard()` already sets `mcd = &mcds[port][mtap.GetMemcardSlot()]` before dispatching the card command, and `_mcd::port/slot` are initialized in `Sio2::Init`. This uses authoritative selected-card identity, avoids unnecessary dependence on `g_MultitapArr`, and preserves the command responses and existing verbose logging.

Source review also confirmed that Sio2::Memcard pre-pushes **two** response bytes before the F0 handler, so the handler's first `0x2B` is at output byte index 3, as expected by `SECRMAN::card_auth_read`. No framing change was made.

**Verification:** The MSVC compiler diagnostic fix is committed but awaits user rebuild. A standalone local compile attempt was blocked by container DNS resolving raw.githubusercontent.com, not by a compiler error; no green test result is claimed.

## Build verification update (2026-10-17)

User confirmed the `bee3eef2` SIO2 card-slot correction **fixed the reported MSVC compilation errors**. This is user-reported build/error verification, not confirmation that the BIOS recognizes PS2 memory cards correctly or that both 0x8F authentication phases pass at runtime. The next test remains a BIOS-only cold boot with HDD disabled and a disposable card, followed by a regression test of the official HDD software.

## BIOS runtime verification: both original card regressions resolved (2026-10-17)

The user clarified that the successful test was **not just a corrected build**: with the new card-authentication implementation, the BIOS now **identifies PS2 memory cards as PS2 cards instead of PocketStations**, and **memory-card formatting works correctly**. This directly verifies resolution of both original observable symptoms on the test setup. The evidence supports the earlier diagnosis that failure of MechaCon 0x8F card authentication caused MCMAN to fall back to PS1/PDA card detection and the PS1 formatter. No SIO2 PS1 detection override or memory-card flash write bypass was needed.

**Verified by user:** PCSX2 fork builds after the selected-card slot fix; PS2 card recognition in BIOS; PS2 card formatting. **Not independently verified:** all cryptographic intermediate results against physical retail-card vectors or hardware; full regression of authenticated HDD Browser/HDD Utility and other titles; automated CI pass (no run status available). Preserve verbose logging and avoid overstating the scope of runtime validation.
