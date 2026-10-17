# PlayOnline Viewer installer advances to online certification (2026-10-17, v6)

Branch: `note-working/magicgate-card-auth-protocol-20261017`. Builds on the card authentication and HDD/KELF test handoffs. No emulator code changes in this update.

## Evidence and observed result

User tested PlayOnline Viewer and Tetra Master disc 2 (USA/Canada), `SCUS-97269`, in PCSX2 `v2.9.92-87-gb414a76dd`. Screenshot on Install screen: **Online Certification Error: `POL-1556 (Network Error -514)`**, with generic request to check connection/settings. The user previously used this installer as a DNAS/installation compatibility oracle because it would not proceed to installation before certification.

Attached log: `Pasted text(20261017-174454).txt`, 26,722 lines (not committed). BIOS v1.60 USA; HDD attached; program ELF `cdrom0:\\SCUS_972.69;1` executes at 30.0885s. Card-authentication SCmd `80–88` exchange completes on repeated occasions (each `8F` has first and second `01 → 00` calculation). Protected HDD Browser KELF header verification also succeeds; no `8F result=80` appears. This demonstrates coexistence of the new card-auth crypto and existing HDD/KELF crypto.

By 106.3–106.4s the software loads `avetcp`, `netdev`, `avendi`, `smap`, `avedhcp` network modules and initializes the SMAP MAC. At ~110.15s it reports 10BaseT half-duplex link. At 111.4398s DEV9 logs **`Socket: Creating New UDP Connection from fixed port 10874 to 53`**, indicating DNS activity; after ~5 seconds another packet is sent around 116.44s. No TCP connection to an online-authentication endpoint appears in the log. User's displayed error is POL-1556 / network -514.

**Conclusion:** PlayOnline's install/certification path now reaches the network stack and initiates DNS traffic. This is material progress past previous local compatibility blockers. **Do not claim DNAS server authentication succeeded or that the full installer completed.** POL-1556 historically appeared during actual DNAS outages, so the UI message alone does not discriminate between DNS failure, server availability, and a later DNAS-layer failure. A packet capture or SIO2/SECRMAN/INET return-code trace could establish the precise endpoint and stage; the present log's UDP/53 evidence suggests initial name resolution is the immediate next target.

## Correct earlier erroneous interpretation of HDD command 0xEC

An earlier HDD game-install handoff was changed to claim `0xEC` is standard ATA `IDENTIFY DEVICE`. Standard ATA command `0xEC` indeed is IDENTIFY DEVICE, **but the PS2 log has two separate command namespaces**: `ATA: C_IDENTIFY_DEVICE` at 11.0228s, and `DEV9: HDD_SCE SONY-SPECIFIC SECURITY CONTROL COMMAND ec` at 98.9420s. The latter is explicitly Sony-specific command `0xEC` in the HDD_SCE security control interface, not the standard ATA register `IDENTIFY DEVICE` command. Preserve this distinction in future DEV9/security analysis and amend the older v5 wording on the next editing pass.

## Next tests (not yet done)

1. Check the network configuration and DNS resolution/response for the request sent at 111.4398s, preferably a capture or explicit logging of destination and DNS response code; avoid speculating whether `-514` is exactly NXDOMAIN, timeout, or transport failure without PlayOnline error mapping.
2. For actual DNAS service authentication, use known compatible server and controlled DNS routing if available; verify a request reaches the server and the server accepts/rejects it. A DNS request is not a completed DNAS exchange.
3. Only mark PlayOnline installer/DNAS compatibility fully verified if online certification advances to HDD installation. Keep existing verbose MG logging untouched.

Reference: https://github.com/ps2dev/ps2sdk/tree/master/iop/security/secrman and historic Square Enix support/forum reports of POL-1556 during DNAS outages.
