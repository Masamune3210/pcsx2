# MagicGate authenticated content implementation handoff (2026-10-17, v2)

Branch: `note-working/magicgate-authenticated-header-20261017` in `Masamune3210/pcsx2`.
Predecessor: `docs/magicgate-authenticated-header-handoff-20261017-v1.md`.

## Already established, do not re-investigate

HDD Utility 1.10 USA (`SCUS-97395`), authentic 0x1A0-byte KELF header accepted twice by physical MechaCon 3.06, BIT SHA256 `7c3d0e4746b9fc11b2691f66146e53e4db0bc111533df34c25186d5b5c42e220`, BIT 20 blocks and 0x5370 content bytes. User's recent PCSX2 trace verifies header, SCmd8F=00, SCmd91=00 48 01, streamed plaintext BIT. The first genuine encrypted 0x400-byte content descriptor has BIT signature `B0 C2 34 14 AD AB 3B D6`; independently decrypted plaintext starts with ELF magic. No published keys, file keys or copyrighted fixture bytes are committed.

The already supplied `Pasted text(20261017-065837).txt` is the baseline for the content path. An existing nine-field `PS20160AC20020207.mgkeys` suffices for header verification but **must include `MG_CONTENT_IV`** to decrypt content. The optional field is 8 bytes as 16 hex characters. Do not silently replace unknown IVs with zeros. The key converter accepts `MG_CONTENT_IV` from the user's existing key JSON and remains fail-safe against overwrites.

## Changes committed to this branch

- `pcsx2/CDVD/MagicGateCrypto.h`: parses optional 8-byte content IV without breaking nine-required-key header parsing. Saves KELF encryption key count from `header.Flags >> 4 & 3`. Adds `ContentCursor`, `ContentResult` and `DecryptContentTransfer`: descriptor cursor, per-descriptor Kc DES/2-key 3DES CBC decrypt with content IV, signature verification, unsigned spans, and transactional failure (no plaintext delivered on a bad MAC). For signed encrypted blocks, MAC XORs decrypted 8-byte words then EDE2 encrypts under header signature master/hash keys. Signed plaintext blocks use DES CBC -> D(hash) -> E(master).
- `pcsx2/CDVD/CDVD.cpp`: retains authenticated header, decrypted BIT, Kbit/Kc across SCmd92. Separate content-worker busy/verified/output-ready fields. Handles 92 input bound, 8D staging, 8F decrypt+MAC, 93 verified-output authorization, 8E plaintext transport, content-descriptor cursor advancement. Critical correction: the old SCmd8D `returnedBusy` rejection was accidentally inherited from header mode; it now applies to header writes only. 8C/90 and VM reset still invalidate transient state.
- `tools/magicgate/SmokeMagicGateCrypto.cpp`: synthetic protected 3DES CBC round-trip, signature checking, ciphertext tampering, optional IV validation, unsigned partial transfers, signed plaintext block and bad-MAC rejection.
- `tools/magicgate/VerifyMagicGateFirstBlock_20261017_v1.cpp`: local-only end-to-end regression, accepting the user's .mgkeys, the authentic 416-byte header and a 1024-byte encrypted content block. Asserts the known BIT descriptor, verified MAC, 0x7F ELF magic and next descriptor cursor. Prints neither file keys nor plaintext.
- `tools/magicgate/ExtractMagicGateContentFromLog_20261017_v1.py`: reconstructs the 0x400-byte first content ciphertext from an existing MGRAW log without requiring new console or emulator capture. Refuses overwrite.
- `tools/magicgate/BuildMagicGateKeyFile.py`: emits `MG_CONTENT_IV` when present in the published-keys JSON; still supports header-only configurations.
- `.github/workflows/magicgate-crypto-standalone-20261017-v1.yml`: compiles standalone tests and private-fixture regression runner without downloading or committing authentic fixtures.

## Local verification procedure

In Visual Studio Developer PowerShell, after updating the local branch and adding the correct `MG_CONTENT_IV` to the existing BIOS-adjacent sidecar (or generating a new sidecar safely):

```powershell
py tools\magicgate\ExtractMagicGateContentFromLog_20261017_v1.py "C:\path\Pasted text(20261017-065837).txt" "C:\path\hdd-first-content-0x400.bin"
cl /nologo /std:c++20 /EHsc /I pcsx2 tools\magicgate\VerifyMagicGateFirstBlock_20261017_v1.cpp /Fe:VerifyMagicGateFirstBlock.exe
.\VerifyMagicGateFirstBlock.exe "C:\path\PS20160AC20020207.mgkeys" "C:\path\HDDMG110.BIN" "C:\path\hdd-first-content-0x400.bin"
```

Then build/relaunch the fork and run HDD Utility. Expect a content `8F` busy response followed by `00`, a `[MG] Verified content: bytes=1024 nextBlock=1 blockOffset=0` message, `93=00`, and 0x400 plaintext bytes delivered through 8E. These are implementation **expectations**, not observed emulator outcomes.

## Verified vs unresolved

Verified *by code inspection and cross-reference against xfwcfw/kelftool*: Kc CBC per block, content IV, signature formulas, and descriptor flags. Existing authentic header and decrypted BIT were already verified on console, in independent host software, and in the emulator per user's handoff.

**Not yet verified in this investigation:** compilation of modified sources, CI success, execution of synthetic tests or authentic first-block runner, actual 0x400-byte content decryption in the emulator, subsequent content blocks, and full HDD Utility boot. The GitHub connector exposes no push-triggered workflow run in its available lookup, so do not claim green CI. Use a fresh runtime log to identify the first actual post-content failure.

Intentional current limitation: each encrypted or signed BIT descriptor must fit entirely within a single 0x92 staging window (so its MAC can be checked before releasing plaintext). Unsigned/unprotected spans can be split across windows. If the HDD Utility splits any protected descriptor, preserve fail-closed behavior and work out the actual MechaCon streaming semantics before allowing unauthenticated plaintext. 3-key 3DES is not implemented; 1-key DES or 2-key EDE is supported according to header flags.

Existing separate limitations: savestate serialization of the volatile worker and memory-card MagicGate commands remain unimplemented. Do not expand scope until authentic content flow has been tested.
