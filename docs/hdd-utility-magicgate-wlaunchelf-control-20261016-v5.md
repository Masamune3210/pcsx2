# HDD Utility MagicGate investigation v5: wLaunchELF reference run (2026-10-16)

Branch: `note-working/magicgate-hdd-utility-header-validation-20261016`. Previous [v4](hdd-utility-magicgate-header-probe-20261016-v4.md). DNAS explicitly parked by user.

## Grounded comparison

User supplied runtime logs for **the same PCSX2 build `v2.9.92-11-g7fc8bf4be`**:
- Official Sony **HDD Utility Disc v1.10**, `SCUS_973.95`, CRC `E2B8E3DC`, log `Pasted text(20261016-174801).txt`.
- Homebrew **wLaunchELF**, ELF CRC `B398581E`, log `Pasted text(20261016-174959).txt`.

Both run against `testdrive.raw` 40GiB / 83,886,080 sectors with BIOS USA v1.60 (2002-02-07), and both execute the `0x90 -> 0x8D repeated -> 0x8F` MagicGate header path. Both headers are successfully accumulated to their requested length without any `0x8D` error, and **both reject header BIT parsing** in `0x8F` with `0x80`:

| | Official HDD Utility | wLaunchELF |
| --- | --- | --- |
| SCMD 0x90 input | `00 A0 01 00 00` | `00 80 00 00 00` |
| Header size | `0x1A0` | `0x80` |
| ELF size in header | `0x5370` | `0xF9C0` |
| Flags | `0x22C` | `0x22C` |
| Encryption zone | USA | all zone bits set |
| BIT offset | `0x48` | `0x48` |
| BIT count | 33350 (`0x8246`) | 13851 (`0x361B`) |
| Reserved bytes | `82 F5 E1` | `36 F8 07` |
| Validation | `BIT_FORMAT_REJECT`, `SCMD 0x8F -> 0x80` | same |
| Outcome | next: restart, TLB PC=0 | next: continues into IOP modules, DEV9, SMAP, USB keyboard |

The wLaunchELF header has literal `rom0:PSXVER\0` at `mg_buffer+0x30` and zero-filled bytes `0x40..0x47`; its BIT candidate at `0x48` is random-looking. The 128-byte buffer **is not known to be a genuine encrypted PlayStation-authenticated KELF**. Do not treat it as a known-good canonical plaintext BIT or infer that the HDD Utility should simply ignore failures.

**Important command-order contrast:** HDD Utility's boot/BIOS sequence runs MechaCon auth (`0x80..0x87`) before its protected header transfer. The wLaunchELF initial protected-header transfer at ~15.759s runs **before** its visible MechaCon auth sequence, which begins ~17.229s. After the wLaunchELF header's failed 0x8F, it continues and later gets 0x8F/0x88 **success `00` with `mg_datatype=0`**, which simply skips the guarded header-validation branch in PCSX2's `cdvdWrite16`; it **is not evidence of successful KELF decryption/authentication**. This matters when reading subsequent `[MGRAW] MG_KBIT/KCON` logs, where buffers remain zero in these calls.

## Implementation implications

We have reproduced the BIT parser rejection with two independent guest programs, but only the HDD Utility depends on success. The BIT mismatch is not a special wLaunchELF-versus-HDD regression and does not indicate host HDD issues. Prioritize tracing why the 0x8F/0x88 header-consumption path lacks the actual decryption/validation expected from MechaCon; use existing analyzed SECRMAN/DECKARD firmware path rather than forging a positive result or mutating the ciphertext. The wLaunchELF case is a negative control, not successful MagicGate reference.

No source changed by this comparison, no Ghidra labels/signatures inferred from runtime alone. Full raw-byte logging patch is now running in the user's local PCSX2 build, while the GitHub connector previously blocked writing that patch remotely. User's observed executable version does not by itself prove the raw logging patch was pushed to this branch.

Next: correlate `0x80..0x87` crypto state changes expected by SECRMAN and DECKARD, then inspect the 0x8F header operation and subsequent decryption/metadata production; do not spend new tests on DNAS.
