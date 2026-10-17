# MagicGate authenticated-header implementation branch (2026-10-17)

Origin: `note-working/magicgate-hdd-utility-header-validation-20261016`. This branch exists to replace the old raw-ciphertext KELF parser with a *verified* external-key DES/3DES header processor and accurate negative-path SCmd state transitions, based on the physical user's MechaCon3.06 results.

## Evidence already established

- User's SCPH-39001 (BIOS1.60 / MechaCon3.06) returned `0x84` for the all-zero 416-byte KELF header and two one-bit corruptions, but accepted the true HDD Utility1.10 416-byte header **twice** with status `00`.
- Both physical accepts yielded exactly 328-byte BIT SHA256 `7c3d0e4746b9fc11b2691f66146e53e4db0bc111533df34c25186d5b5c42e220`.
- Offline independent public-key DES/3DES computation independently verified the *actual* header MAC, derived file keys, decrypted exact matching BIT, BIT MAC and root MAC. The 20 block lengths sum to header content size `0x5370`.
- Full private fixture/key bytes are held by the user, not to be committed; independent evidence and KELF layout are saved under Masamune3210/ps2mgdecomp `reports/2026-10-17-scph39001-authentic-hdd-kelf-full-crypto-crossvalidation-v1.md`.
- The original PCSX2 branch has useful `MGRAW/MGFLOW` tracing; currently `0x8F` tries to parse encrypted BIT as plaintext, `0x8D` rejects against declared rather than physical staging capacity, `0x91` exposes ciphertext, and key replies are unauthenticated.

## Decisions

- Build a cleanroom, standalone testable DES/3DES and KELF verifier with public-file-format constants, with published keys supplied *only* by a separate user-owned key file. Never check key literals or copyrighted original KELF bytes into Git.
- Put authenticated BIT bytes into the existing `0x91/0x8E` transport *only after* signatures and structure verify; never shortcut success for a known SHA.
- Reset transient state at VM reset/SCmd8C and SCmd90; keep 0x80-0x88 card authentication marked separate and incomplete.
- Preserve older malformed-request and negative-hardware results; differentiate busy and terminal status without claiming exact hardware timing.
- Execute independent host KATs against local, uncommitted genuine fixture; do not claim a full PCSX2 build if not compiled.

## State / next action

**Branch created, implementation pending at handoff creation.** Inspect only the existing CDVD command cases and header include points; implement the independent module, wire SCmd state, run host fixture plus seven mutation checks, then commit checked changes. Record actual tested facts and unresolved full-system behavior here, rather than assuming completed integration.

## Implementation milestone (2026-10-17)

**Committed in this branch:**

- `pcsx2/CDVD/MagicGateCrypto.h`: cleanroom standard DES, two-key EDE, CBC, header MAC, two independent file-key unwrapping operations, BIT decrypt and structure validation, BIT MAC, and root MAC. No keys or copyrighted KELF fixture embedded.
- `pcsx2/CDVD/CDVD.cpp`: SCmd8C volatile reset; 90 length/mode validation; 8D distinct 0x800 staging cap, raw 8E transfer, 8F busy then real authenticated header result, 91 decrypted BIT only after success, authenticated-only 94..97 key results with MGRAW key-value redaction. 88 is explicitly separate from the header worker; it remains a card-authentication placeholder, **not verified card cryptography**.
- `tools/magicgate/BuildMagicGateKeyFile.py`: convert user's published keys JSON to a KEY=HEX plaintext file sharing the selected BIOS name, extension `.mgkeys`. The converter refuses overwrite and never prints key values.
- `tools/magicgate/SmokeMagicGateCrypto.cpp` and `.github/workflows/magicgate-crypto-standalone-20261017-v1.yml`: public synthetic DES/2-key EDE/CBC known-answer/inverse tests and malformed-keys/zero-header rejection, compilable without BIOS or private inputs.

**Host verification actually performed:** the portable DES/3DES code, compiled with `g++ -std=c++17 -Wall -Wextra -Werror -O2`, successfully authenticated the authentic uncommitted HDD Utility 1.10 416-byte input, decrypted exactly 328 BIT bytes, and generated SHA256 `7c3d0e4746b9fc11b2691f66146e53e4db0bc111533df34c25186d5b5c42e220`, independently matching two physical MechaCon3.06 results. All three cryptographic signatures match, BIT describes20 blocks/0x5370 bytes, seven one-bit variants failed closed, malformed key config rejected. A separate synthetic smoke test passed locally. **This is not yet a full PCSX2 build or end-to-end runtime test**; no GitHub Actions result should be described as green unless observed.

**How to use:** on the new branch build PCSX2, then create `<selected-BIOS-stem>.mgkeys` adjacent to the BIOS via:

```powershell
py tools/magicgate/BuildMagicGateKeyFile.py "C:\path\MagicGate_public_keys_user_supplied_20261017_v1.json" "C:\path\your-selected-BIOS.bin"
```

Restart PCSX2, run the same HDD Utility 1.10 image and inspect `[MG] Verified KELF header: ...`, `0x91` BIT length 0x148, and subsequent commands. Missing/malformed keys produce fail-closed 0x80; invalid MAC produces 0x84. A public-key file must **never** be committed into the source branch.

**Remaining:** Build the full Windows emulator, run authentic HDD Utility, verify the new SCmd path at runtime, then implement full content block crypto/signatures and the unrelated memory-card authentication path. Investigate savestate serialization of `s_mgHeader` before merging to testing; it is currently reset on VM reset but is **not serialized**. Do not equate one fixed busy reply with exact hardware time, and do not claim card authentication has been replaced. DNAS work remains parked.

Original branch-specific source tracing is preserved, and no raw firmware/keys were committed.
