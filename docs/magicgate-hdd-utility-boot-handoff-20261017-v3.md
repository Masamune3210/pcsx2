# MagicGate HDD Utility boot handoff (2026-10-17, v3)

Branch: `note-working/magicgate-authenticated-header-20261017` in `Masamune3210/pcsx2`.
Predecessor: `docs/magicgate-authenticated-content-handoff-20261017-v2.md`.

## Outcome: first successful HDD Utility installer UI

After correction of the authenticated-content BIT cursor, the user supplied a screenshot of HDD Utility Disc v1.10 (USA, SCUS-97395) displaying **"Getting Started 1/2"** and **"This step formats the HDD and installs the HDD software."** This is runtime confirmation that execution now reaches the actual Sony installer interface, beyond the previous PS289.VOB trap. It does **not** prove HDD formatting/installation completion, complete MagicGate compatibility, or memory-card MagicGate functionality.

## Root cause of previous post-PS289.VOB execution failure

The decrypted, verified KELF BIT has 20 descriptors totaling **0x5370** bytes of content. The installer passes **only the protected descriptors (nonzero BIT flags)** to the MechaCon via SCmd92/8D/8F/93/8E. Unprotected descriptors are copied by the loader itself, not supplied to the MechaCon's authenticated-content worker.

The earlier `DecryptContentTransfer()` mistakenly advanced through unprotected BIT descriptors by consuming the input stream. After the four 0x400 protected blocks, it misclassified subsequent ciphertext as unprotected plaintext: the content MAC was not checked for those later protected descriptors, and invalid instructions/trap ensued downstream. This was a cursor mapping defect, **not** a general R5900 instruction-decoder deficiency.

Correct protected descriptor indices: **0, 1, 2, 3, 5, 7, 9, 11, 13, 15, 17, 19** (12 descriptors). Authenticated transfer size is **0x1090**, while total content is **0x5370**. The remaining **0x42E0** bytes are unprotected loader-copied content. The four initial 0x400 encrypted/MACed transfers plus later smaller 0x10/0x20 transfers match this mapping.

## Fixes committed on this branch

- `f637e78b37b1d69d8586493cba3c0826348778e7` in `pcsx2/CDVD/MagicGateCrypto.h`: skip BIT descriptors with flags == 0 without consuming MechaCon content input.
- `fae1d4df53f82e93e728eddd2ac99fa9b7e68a1e` in `pcsx2/CDVD/CDVD.cpp`: remaining SCmd92 transferable-byte bound counts protected descriptors only.
- `c19c94e04580e3690372a0710dec2f52ba41dec6` in `pcsx2/CDVD/MagicGateCrypto.h`: clarify protected-only transport assumptions.
- Earlier parser commits `ce4f6bc63d55c16903b855ae9de36eceb18c6e2e` and `7e685e1843cd8b77f4d152137fc0e1c77209429e` allow extra named .mgkeys fields to be ignored and logged, without rejecting recognized keys.
- Earlier logging-path diagnostic `c92a32f6905177a9777f6665535dbc98e0411ce6` confirms PCSX2 logs at `<EmuFolders::Logs>/emulog.txt` when file-open succeeds.

## Evidence and verification limits

- User runtime log from pre-cursor-fix build (PCSX2 v2.9.92-62-gc92a32f69) confirmed `[MG] Verified KELF header: blocks=20 BIT bytes=328 content bytes=21360`; protected 0x400 bytes initially verified; later transfers succeeded erroneously under wrong cursor mapping. This run trapped after the PS289.VOB ELF handoff.
- New screenshot after the cursor fix shows the HDD Utility's functioning "Getting Started 1/2" page. This directly verifies substantial further execution progress. There is no accompanying new full emulator trace or independent local compilation/CI result recorded here.
- Header reference: authentic 0x1A0-byte KELF; BIT SHA256 `7c3d0e4746b9fc11b2691f66146e53e4db0bc111533df34c25186d5b5c42e220`.
- No cryptographic key material or copyrighted content bytes are committed.

## Next steps

1. Allow the user to exercise the real HDD Utility installer on their **disposable test HDD image**; inspect new runtime logs or UI if another fault appears. Never assume formatting was performed from the introductory page alone.
2. Compare successful per-descriptor MAC verification, plaintext output, and cursor evolution against real-console reference where needed. Add a regression asserting skip of unprotected BIT descriptors and the protected-only size bound, without embedding copyrighted fixtures.
3. Once installation works, consider additional MagicGate workflows and remaining memory-card auth/savestate state only on evidence. Avoid scope drift.

**Logging constraint:** The detailed MGRAW/MGFLOW dumps are intentionally oververbose instrumentation while debugging. The user explicitly instructed that logging be left alone. Do not reduce, deduplicate, or disable those traces.
