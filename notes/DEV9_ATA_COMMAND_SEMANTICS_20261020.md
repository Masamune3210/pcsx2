# Remaining command semantics, 2026-10-20

Scope: source audit of `ATA_CmdNoData.cpp`, transfer-mode selectors, volatile
configuration reset policy, and the mandatory Power Management feature set.
The reference is local T13/1153D revision 18,
`/workspace/scratch/ata4-reference/d1153r18.txt` (the primary draft already
identified in the command comparison). No host ATA capability probing applies to
this image-backed virtual drive.

## Corrections

- Implemented optional SET FEATURES `66h` / `CCh`: disable / enable reverting to
  defaults after software reset. The flag is `fetRevertToDefaults`, initialized
  true and returned to true by hardware reset. Section 8.37.16 explicitly says
  `66h` allows modified settings to remain unchanged after software reset.
- Default PIO mode is now 2, and the hardware-reset Multiword DMA default is now
  0. Section 10.2.1 requires devices advertising PIO modes 3/4 to power up in
  mode 0, 1, or 2; section 10.2.3 requires Multiword DMA mode 0 at power up.
  SET FEATURES transfer selector `00h` chooses that same default PIO mode.
  Advertised supported PIO, Multiword DMA, and Ultra DMA ranges remain intact.
- Reset integration is in `ATA_State.cpp::ResetEnd` (coordinated with the main
  implementation): with reversion disabled, SRST retains selected PIO/DMA mode,
  current CHS translation/validity, multiple block size, write-cache enable state,
  and programmed standby period. With reversion enabled, SRST restores these
  volatile settings to defaults. The standby countdown restarts at the retained
  period on reset; reset timing is an explicit virtual-drive policy.
- Ultra DMA enable/disable state and the selected Ultra DMA mode remain unchanged
  across SRST regardless of `66h`/`CCh`, as required by section 9.3.1(e). Hardware
  reset clears selected Ultra DMA and restores selected Multiword DMA mode 0.
  SMART enable/autosave are persisted drive settings and remain independent of
  volatile default reversion (sections 8.41.1/8.41.2).

The volatile power-on defaults are PIO 2, Multiword DMA 0, Ultra DMA disabled,
CHS translation 16 heads/63 sectors, multiple block size 128, write cache enabled,
and standby timer disabled. SRST preserves the current power mode except that
Sleep becomes Standby; hardware reset chooses Active. Section 6.8.2 recommends
preserving the previous power mode after reset, while figure 6 path 6 explicitly
allows a vendor-specified Active/Idle/Standby result. This does not assert physical
spindle timing for an image-backed device.

## Examined and retained behavior

- Section 8.37.10/table 20: selectors `00h`, `08h..0Ch`, `20h..22h`, and
  `40h..46h` are the implemented modes. The final Ultra DMA modes retain the
  existing later-ATA extension. IORDY-disable selector `01h` is aborted because
  that capability is not advertised; all PIO modes below mode 4 are accepted.
  Selecting PIO leaves DMA selection unchanged; selecting either DMA family
  disables the other family, preserving one selected PIO and one selected DMA.
- Sections 8.37.2/.6: other SET FEATURES subcommands are optional for this fixed
  image-backed device; unsupported/invalid subcommands abort. Read look-ahead,
  Advanced Power Management, removable-media notification, and PACKET release/
  SERVICE interrupts are neither implemented nor advertised. Accepting them
  without meaningful behavior would invent capability. Retired 8-bit transfers
  remain unsupported.
- Section 8.37.9: write-cache disable already waits for the write queue and durable
  flush before completion. Cache enable/disable now participates in the explicit
  volatile reset policy.
- Section 8.39.4/.6: multiple counts 2, 4, 8, 16, 32, 64, 128 are accepted; invalid
  counts abort without changing the previous setting. Count zero remains the
  documented compatibility extension disabling multiple transfers; this is not
  presented as an ATA-4 table input.
- Sections 6.8.2/.3 and 8.6: CHECK POWER MODE does not change the state or restart
  the standby timer. Other accepted commands restart the inactivity period.
- Sections 8.14 and 8.42/table 14: timer encodings 0, 1..240, 241..251, 252, 253,
  255 are already decoded correctly; 254 is reserved and aborted. Code 253 uses
  the explicitly allowed implementation choice of 10 hours. IDLE/STANDBY program
  the timer; their immediate variants retain its configured period. Timer time
  comes from IOP cycles; internal completion pumps do not advance it.
- Section 6.8.1/.2 and 8.40: Sleep blocks command/task-file writes until reset;
  media accesses resume Active from Idle/Standby. There is no basis here for a
  fictitious physical host power capability check.

## Prospective regressions and actual checks

Added source cases near the existing SET FEATURES/power tests for all 256 transfer
selectors; `66h` preservation and `CCh` restoration of volatile settings; hardware
reset overriding `66h` and re-enabling reversion; unconditional Ultra DMA SRST
preservation; and boundary timing for every table-14 encoding family (including
the 10-hour choice and disabled timer). Existing selector tests now cover all
unsupported optional feature families and verify the default PIO selector.

Tests were written before the implementation edits, but **no configuration,
compilation, C++ executable tests, emulator run, or guest boot was performed**, as
requested. Source diff/whitespace inspection is the available verification.
Runtime conformance and Windows validation remain unrun.
