# HDD save-state policy, 2026-10-20

> **SUPERSEDED / REVERTED (2026-10-20).** At the user's explicit request, the HDD-enabled savestate ban described below was removed. `SaveState.cpp`, `SaveState.h`, and `VMManager.cpp` were restored byte-for-byte to their pre-audit versions in commits `74a6308c057f`, `6980793d5957`, `8a8c4d4b9873`. Normal manual, scheduled diagnostic, and resume savestates are no longer blocked when HDD emulation is enabled. The user understands that external HDD images are neither captured nor rolled back. This document is historical rationale **not an active policy**. Do not reintroduce the ban without an explicit request.



## Finding F13 and bounded policy

The archive saves CPU, IOP and guest memory without corresponding DEV9/ATA logical state, FIFO bytes/counters, pending IOP DMA ownership, ATA buffers or the write queue. It also cannot roll back writes accepted by the external HDD image. Restoring guest filesystem memory against a later image generation is therefore an unsupported recovery operation, even if a command happened to be idle when the archive was created. Serializing pointers, FILE handles, callback addresses, mutexes or worker threads would not solve external-image consistency.

This checkpoint rejects both state capture and state load whenever the current `EmuConfig.DEV9.HddEnable` is true. The shared `SaveState_CheckHDDStateSupport(Error*)` returns the normal error:

> Save states are unavailable while the emulated HDD is enabled because the disk image is not saved/restored.

This is an explicit loss of functionality, including manual slots/files, PSBBN scheduled autosaves and shutdown resume saves while HDD emulation is enabled. Existing archives cannot be loaded with HDD emulation enabled. The policy applies even to archives originally created without an HDD; no reliable archive/device provenance is currently available. It does not change the archive version or format, modify an image, delete old archives, or attempt snapshot rollback.

**Turning HDD emulation off to bypass the restriction does not make an old HDD guest state coherent or recover its filesystem.** It restores neither the missing device state nor the disk generation. Cold boot and independently preserved disk evidence remain the supported investigation path. A future implementation must define coherent image-generation handling as well as logical DEV9/ATA serialization before this restriction can be relaxed.

## Entry points and caller behavior

- `SaveState_DownloadState` checks policy before allocating the archive buffer or calling any freeze/serialization functions. `VMManager::DoSaveState` already treats a null result as an ordinary error and returns through the supplied error callback before screenshots, backup renames, archive creation or compression-thread creation.
- `SaveState_UnzipFromDisk` checks before opening/parsing the archive, `PreLoadPrep`, internal-state deserialization or guest-memory restore. Direct users receive false and the error without invoking the loader's reset-after-corruption paths.
- `VMManager::DoLoadState` checks before audio flush and `Host::OnSaveStateLoading`, covering manual slot loads and resume/startup load calls.
- `VMManager::LoadState(filename)` also checks at its public entry. Its existing generic failed-load branch calls `Reset()`; allowing a deliberate policy rejection to reach that branch would disturb the running guest despite rejecting before deserialization. The new preflight returns false directly and retains the normal frontend error reporting.
- Resume/startup load rejection follows the existing `StartupFailure` path and `Shutdown(false)`; startup has already initialized the VM but does not restore archive data. A normal cold boot remains available. Normal startup/teardown still owns HDD handles and lifecycle; the new policy helper performs no HDD I/O.
- Shutdown resume-save rejection reaches the existing `SaveResumeState` OSD callback and shutdown continues. An older resume archive remains on disk and is likewise refused on subsequent resume while HDD is enabled.
- PSBBN scheduled autosaves reach the existing failure callback and produce no archive or compression worker. The inherited scheduler still advances its attempt count and resets its bounded diagnostic observer before requesting capture; this checkpoint does not alter that unrelated scheduling/debug behavior. Repeated failed attempts can reach its configured limit.
- A compression worker already holding an archive captured while HDD emulation was disabled may finish writing that immutable capture. It does not collect state on the worker thread or represent an HDD checkpoint. The policy is deliberately checked on the core capture/load path, not by reading mutable `EmuConfig` from the compression thread.
- Screenshot-only archive browsing remains available because it does not restore guest state.

## Compatibility limits and remaining work

This is a guard on the current configured device, not a new disk snapshot system or archive provenance scheme. In particular, loading an old HDD archive with HDD disabled is still an unsupported recovery technique. `SaveStateBase::vmFreeze` can restore ELF/CRC metadata and invoke `HandleELFChange(true)` to reload game settings; a profile that enables HDD during that later settings change is outside the initial-enabled preflight guarantee. The rejection introduced here executes before any state parsing when HDD is already enabled. Coherent archive/device provenance and preventing later HDD enable after an unsupported restore require further design.

Runtime verification must include enabled/disabled HDD combinations; manual file/slot rejection preserving PC, RAM, ATA/FIFO state and disk hash; direct capture/load API calls; queued PIO/DMA and acknowledged queued writes; save backup preservation; scheduled autosave failure callbacks; shutdown/resume errors; missing/corrupt archives; and normal state save/load with HDD disabled. Preserve disposable disk clones and do not use a real installation image for rollback experiments.

## Checks actually performed

- Read F13 and traced the archive entry list, freeze order, manual file/slot callers, scheduled saves, resume save/load and compression-worker ownership.
- Reviewed the actual diff to verify guards precede archive allocation/open, freeze/load preparation, load callbacks and the public file-load reset branch.
- `git diff --check -- pcsx2/SaveState.cpp pcsx2/SaveState.h pcsx2/VMManager.cpp` passed.
- No compiler, test executable, save/load session, Windows build or PSBBN run was executed. No runtime behavior, disk-hash preservation or frontend presentation has been demonstrated. This follows the user's explicit no-compiling requirement; the parent task also prohibited test execution for this bounded change.
