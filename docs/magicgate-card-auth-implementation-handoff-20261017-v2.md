# Card ↔ MechaCon MagicGate authentication: implementation handoff (2026-10-17, v2)

Branch: `note-working/magicgate-card-auth-protocol-20261017`
Parent: `note-working/magicgate-authenticated-header-20261017` (HDD Utility/KELF working).
Earlier research handoff: `docs/magicgate-card-auth-protocol-handoff-20261017-v1.md`.

## Why this branch exists

The user bisected PS2 cards erroneously appearing as PocketStations and failed 8 MB card formatting to `b23b1cb69`, which introduced strict KELF-only 0x8F behavior. An HDD-disabled BIOS-only trace shows `0x80=00, 0x81=00, 0x82=00, 0x83=00, 0x8F=80`; original OSDSYS runs and HDDLOAD fails as expected without an HDD. ps2dev SECRMAN shows SCmd 0x8F completes **two** card authentication phases, not merely KELF. ps2dev MCMAN shows a PS2 authentication failure (-90) causes PS1/PDA fallback and sends formatting to `mcman_format1`, which writes 128-byte pages, consistent with the observed corruption warnings.

A temporary one-shot 0x8F success patch was fully reverted on sibling `note-working/magicgate-bios-card-auth-completion-20261017`; it is **not** part of this branch.

## Implementation committed

- `fe0c33b5` `pcsx2/CDVD/MagicGateCrypto.h`: parse five optional retail card-key fields from BIOS-adjacent `.mgkeys`, using aliases verified in ps3mca-tool: `MG_CARDIV_0` (8), `MG_CARDKEY_0` (16), `MG_CARDIV2_0` (8), `MG_CARDKEY2_0` (16), `MG_CHALLENGE_IV` (8). Require a complete set for card auth. Existing nine required KELF fields and optional `MG_CONTENT_IV` retain their previous rules. Missing card fields do not break KELF verification. No key data committed.
- `fe10c2a3` `pcsx2/CDVD/MagicGateCardAuth.h`: new header-only card and MechaCon runtime, separate roles, per-slot card state, external-key-only 3DES. Uses ps3mca-tool crypto: derive 16-byte unique key from (CardIV XOR CardMaterial) through two 3DES-CBC keys/IVs; generate three chained challenges; card independently decrypts/validates incoming challenges; card emits encrypted responses; MechaCon decrypts responses and checks they contain card and Mecha nonces. Card IV/material and nonce are **synthetic emulator values**, not raw key material copied from a physical card. This is an emulated MagicGate-capable card model, not emulation of a particular physical card's immutable identity.
- `f6a916fd` `pcsx2/CDVD/CDVD.cpp`: connect SCmd 0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88 to state engine; route 0x8F between card and existing KELF workers by explicit selection; poll 0x8F once busy then 0x00 only after a completed calculation or return 0x84/0x80 for invalid stages. Reset session on `cdvdReset`; 0x90 KELF start clears selected card mode. No KELF verification bypass or changes to MGRAW logging.
- `56f2f412` `pcsx2/SIO/Memcard/MemoryCardProtocol.cpp`: wire F3 card reset, F7 retail key selection, F0 00–14 stage progression. Checks write XOR checksums and proper order, returns 8-byte reads reversed with checksum matching `CardAuth.c` framing, rejects invalid stage with 0x66 without certifying; no fake success. Does not change PS1 read/write commands, card formatting or flash backend.
- `b59674b8`: verify challenge3 against CardIV already at F0 0A, matching the point where real cards can reject incorrect keys.
- `4f7fd63b`, `9b3aedc1`: SCmd 0x80 reset succeeds independently of external key availability; subsequent F7 key selection fails honestly when card keys absent (unlike an incorrect early reset failure).
- `8b1f343d`, `f0f86179`: synthetic standalone regression for correct two 0x8F polling phases, response byte tampering, invalid challenge, unavailable keys, and unsupported key mode; file `tools/magicgate/SmokeMagicGateCardAuth_20261017_v1.cpp`.
- `2712f077`: extend `.github/workflows/magicgate-crypto-standalone-20261017-v1.yml` to compile/run new test with C++20 and `-Wall -Wextra -Werror`.

## Protocol mapping

SECRMAN MCMAN callback sets SIO2 card command transport. Card F3 reset; Mecha SCmd 80 reset (loads keys); F7 key-mode 1; F0 00 begins; SCmd81 cnum; F0 01 CardIV / 02 material; SCmd82 derives unique key; F0 04 nonce; SCmd83 nonce; F0 05 then 8F busy=01, success=00 first pass; SCmd84/85 returns challenge; F0 06/07/0B writes challenge3/2/1, card cryptographically checks it; F0 0F/11 gives first two encrypted responses; SCmd86 stores; F0 13 third response; SCmd87 stores; 8F busy=01 then success=00 only if decrypt checks; F0 14 card finalize; SCmd88 completes. Missing keys, invalid ordering, modified challenge, or modified encrypted responses do not authenticate.

Reference:
- https://github.com/ps2dev/ps2sdk/blob/master/iop/security/secrman/src/secrman.c
- https://github.com/ps2dev/ps2sdk/blob/master/iop/security/secrman/src/MechaAuth.c
- https://github.com/ps2dev/ps2sdk/blob/master/iop/security/secrman/src/CardAuth.c
- https://github.com/ps2dev/ps2sdk/blob/master/iop/memorycard/mcman/src/main.c
- https://github.com/israpps/ps3mca_tool/blob/main/src/mecha_emu.c
- https://github.com/israpps/ps3mca_tool/blob/main/README.MD

## Verification status, known limits and next action

**Not yet verified with a PCSX2 build or runtime BIOS.** GitHub standalone regression job was committed but connected GitHub API did not return an Actions status/run result. No independent compile result is claimed. All commits applied successfully in GitHub; a Windows fork build and BIOS-only runtime check are still required.

- Cold-boot *HDD disabled* with same 8 MB PS2 card and .mgkeys containing all five optional card fields. Observe two 0x8F phases, a complete SCmd 84–88 exchange, correct card recognition and normal card formatting. Test using a **disposable copy** of any card previously hit by PS1 formatting.
- Then re-enable HDD and check the previously successful authenticated HDD OSDSYS loads, the HDD Utility installer (read-only if practical), and DVD Player memory-card installer detection. Logging is deliberately oververbose and must remain unchanged.
- If build fails, fix exact compile diagnostics without guessing card behavior. If runtime fails, compare per-phase SIO2 and MGRAW bytes to SECRMAN and ps3mca-tool. Pure-engine roundtrip alone does not validate timing, SIO2 framing in a retail BIOS, or true retail-card compatibility.
- Externally supplied card keys must correspond to the CEX/retail mode selected by F7=01. Code deliberately rejects other key modes (DEX/arcade not yet modeled).
- Current virtual card identity and nonces derive from emulated card slot and a session counter (not a physical card serial); a long-term card-identity persistence / savestate-state plan remains open. Existing KELF worker is already static and not fully savestated. This branch also does not implement F1/F2 authenticated KELF binding to a card, only the SECRMAN F0 card-authentication sequence.
- Missing/partial card fields do not block the older KELF path, but the current CDVD loader still requires the nine KELF fields to accept `.mgkeys` at all. If needed, decouple card-only key parsing in a separate follow-up.

This branch contains source and tests, not a claim that the bug is fixed until the BIOS-only regression passes.
