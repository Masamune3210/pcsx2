# PlayOnline DNAS investigation v11: decrypts __net HDD partition pathname

Branch: `note-working/magicgate-scmd-boundary-trace-20261016`. Previous [v10](playonline-dnas-identity-inputs-20261016-v10.md).

User supplied code from both PlayOnline Viewer second-disc EE programs, matching path-builder functions CD 0x001B7480 and HDD 0x001B7580. This is **NOT** first-disc FFXI INSTALL.ELF. Each decodes a static DES-protected prefix, appends literal `,,` and 8 runtime bytes, NUL terminates, and returns path for a file-open/seek/transfer/close caller CD 0x001B8900 or HDD 0x001B8A00.

Recovered from **exact user-provided executable bytes** with DES ECB key ASCII `EP395gbq` (CD key address 0x00205680, HDD 0x002057B0):

- `cdrom0:\SYSTEM.CNF;1`: CD ciphertext 0x00205690 / HDD 0x002057C0
- `pfs0:`: CD 0x002056A8 / HDD 0x002057D8
- `pfs0:/etc/access_flag`: CD 0x002056B0 / HDD 0x002057E0
- `hdd0:__net`: CD 0x002056D0 / HDD 0x00205800
- Static plaintext suffix `2` at CD 0x002056C8/HDD 0x002057F8 appended to access_flag -> `pfs0:/etc/access_flag2`; static `,,` at CD 0x002056E8/HDD 0x00205818 appended after __net before 8 dynamic bytes.

CD path builder `0x001B7480` or HDD `0x001B7580` obtains dynamic 8 bytes from CD `0x001B7178` or HDD `0x001B7278`, making `hdd0:__net,,<8-byte suffix>`. That lower helper invokes libcdvd S-command RPC framework using numeric parameters 0x1CB and 0x3B4. Their exact semantics, password payload, and hardware source remain unverified. Public PS2 Developer Wiki [HDD page](https://www.psdevwiki.com/ps2/Hard_Drive) lists __net 8-byte FPWD/RPWD and maps `__net/etc/access_flag` to LBA 0x41008, `access_flag2` to 0x4100C and `access_flag25` to 0x41010, comparing 16 bytes in two sector reads. Hence existing PCSX2 `[DNASLBA]` instrumentation is relevant, but no emulator runtime branch confirmed yet.

Ghidra continuation [v29](https://github.com/Masamune3210/ghidra-emotionengine-reloaded/blob/testing/docs/PS2_MagicGate_Continuation_Handoff_20261016_v29.md) records full directly-applicable metadata; user-facing `ApplyPlayOnlineNetPartitionPath_20261016_v6.java` prepared, Ghidra runtime pending. No emulator source changed. Next target is correlation of observed S-command RPC response bytes, actual APA __net partition open, access flag choice and PCSX2 sector reads. Avoid inventing genuine Sony HDDID or claiming -404 hardware root cause solely from static code.
