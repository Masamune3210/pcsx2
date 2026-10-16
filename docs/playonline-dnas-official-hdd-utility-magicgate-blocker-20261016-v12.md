# PCSX2 MagicGate continuation v12: official HDD Utility blocked on MagicGate before clean baseline (2026-10-16)

**Branch:** `note-working/magicgate-scmd-boundary-trace-20261016`. Previous [v11](playonline-dnas-net-partition-des-path-20261016-v11.md).

User switched from noncanonical hacked-HDDOSD image to blank 40-GB PCSX2 HDD `testdrive.raw` and attempted **official HDD Utility Disc v1.10 USA SCUS-97395** to establish an official `__net/etc` access-flag baseline. PCSX2 logs recognition of DEV9 ATA HDD (83,886,080 sectors), BIOS `rom0:ATAD`, `rom0:HDDLOAD -osd`, ATA identify `PCSX2-DEV9-ATA-HDD`, `I-security 0000`, earlier `HDL:read 0 sec` + `ATA: C_READ_SECTOR` + `cannot alloc memory` (unknown causality). Utility's protected ELF loading path then logs exactly:

```text
[MG] hcode=0 cnum=0 a2=0 length=0x1A0
[MG] ELF_size=0x5370 Hdr_size=0x1A0 unk=0x100 flags=0x22C count=0 zones=USA
[MG] ERROR - Make sure the file is already decrypted!!!
```

After restart, `# Syscall: undefined (0)`. **No proof that official formatting, partition creation or access-flag generation took place.** Do not treat current on-disk markers from hacked image as official behavior. The exact MG message and header values were previously reported against a different official DVD Player disc in upstream [PCSX2 issue #8432](https://github.com/PCSX2/pcsx2/issues/8432), whose maintainer attributed it to MagicGate unsupported by upstream for potential legal reasons (Mar 2023). Same symptom does not prove same protected binary or exact failure mechanism.

This represents a **separate MagicGate KELF load blocker** from the observed PlayOnline `SCUS_972.69` DNAS `-404` -> `POL-1562`. Need distinguish them during implementation; no basis to conflate them. Keep source-verified DES-obfuscated `hdd0:__net` / `pfs0:/etc/access_flag2` findings and pending `[DNASLBA]` trace. If pursued, inspect KELF load path and MagicGate implementation requirements. No source modifications this pass.
