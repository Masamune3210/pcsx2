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
