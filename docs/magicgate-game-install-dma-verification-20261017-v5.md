# Dark Cloud 2 official HDD installation and MagicGate test (2026-10-17, v5)

Branch: `note-working/magicgate-card-auth-protocol-20261017`. Supersedes the incomplete gameplay interpretation in `docs/magicgate-hdd-regression-verification-20261017-v4.md`, which counted authentication and launches but did not distinguish installation activity.

## User clarification

The Dark Cloud 2 session in `Pasted text(20261017-164638).txt` explicitly included **installing the game** onto the official Sony HDD installation. It was not just gameplay after HDD Browser launch. This explains the extraordinary volume of DEV9 write traffic. The user's observation that PS2 HDD installations can be tied to a particular console is a worthwhile separate authentication hypothesis, but the log must distinguish HDD software KELF verification from actual game data copying.

## Quantified trace evidence

- Build `v2.9.92-87-gb414a76dd`; official HDD image `testdrive.raw`, 40,960 MiB.
- After successful authenticated HDD Browser launches (at ~65.757s and ~221.158s), disc-based `SCUS_972.13`, CRC `1DF41F33` (Dark Cloud 2), enters game at ~236.100s. The emulator issues Sony-specific HDD security-control command `0xEC` at ~237.888s.
- **Two full memory-card authentication sessions** at ~245.598s and ~248.012s, both through the two-stage 0x8F challenge-response and 0x88 completion (no observed failure).
- During ~237–737s, `121,793` `DEV9: HDD_WriteDMA` commands and `216,712` `*DEV9writeDMA8Mem: size <hex>` callbacks occur. Summing reported hex byte counts in these callbacks yields **1,568,777,728 bytes = 1,496.1 MiB** of HDD write traffic in this window. Bulk writes visibly take place from ~261.5s to ~625.5s. This is consistent with published reports that Dark Chronicle/Dark Cloud 2 installs approximately 1.5 GB of HDD data. The count is *host-to-ATA traffic*, not proven unique partition allocation: it can include metadata, repeat writes, padding, and retries.
- **Zero** MechaCon SCmd `0x90` (KELF header start) and **zero** `0x92` (KELF block start) while the game is running during ~237–737s. Those command types occur in the preceding/following HDD Browser boots, where protected HDD executable decryption was already verified: two `0x90` and 54 `0x92` per complete Browser startup.
- The title session ends with a hard reset at ~736.5s. The authenticated HDD Browser boots again at ~744.131s. The game is then launched again via disc at ~759.040s, with further card authentication around ~768.5s and ~770.9s.
- The log contains no `(FileMcd) Warning: writing to uncleared data` diagnostics. User explicitly confirmed card detection and formatting correct before this integrated run.

## Interpretation / important limitations

This is **stronger integration coverage** than merely running a game after the HDD Browser. Sony HDD security-control protocol, sustained multi-gigabyte game-data writes, and repeated MagicGate card authentication all function in the same runtime. It is *not* direct evidence that every bulk HDD data write is individually MagicGate-encrypted, or that Dark Cloud 2's data partition was cryptographically bound to this MechaCon. Some HDD-bootable KELFs are console-bound, but those are different from bulk asset caches installed for disc-based games.

To establish any per-console binding specifically for this game install: inspect the resulting APA/PFS partition and boot records, then compare installation identity/key-dependent metadata or perform a controlled console-identity/keyset change on a cloned HDD. Do not alter the working image or infer an encryption scheme from successful installation alone.

## Verification state

User-verified: card identity and formatting; 16 total successful MagicGate card sessions in full trace; six HDD Browser KELF headers verified; 162 protected-block transfers; three HDD Browser boots; completed Dark Cloud 2 bulk HDD write workload. The game-install activity is newly quantified here. No emulator code or intentional MG logging changed.

Public technical context: `ps2homebrew/hdl-dump` distinguishes `boot.kelf` and `boot.elf` for HDD Browser boot entries; Dark Chronicle HDD install is described as roughly 1.5 GB in lists of PS2 HDD-compatible games. These references corroborate the distinction but do not independently attest the user's exact HDD metadata.
