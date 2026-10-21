# EE execution profile Qt selector: implementation handoff v34

Repo: `Masamune3210/pcsx2`
Branch: `note-working/psbbn-jit-reiserfs-mmu-witness-20261020`
Previous objective: `notes/PSBBN_EE_PROFILE_SCOPE_DECISION_20261021_v33.md`.

## User-reported defect

The boot-only `EmuCore/EEExecutionProfile` setting existed in VMManager but had **no Qt UI**, making it hard to activate Extended for BB Navigator's HDD-only boot. The 2026-10-21 test logged `EE execution profile: Standard (setting=Auto, boot disc serial=20020207-164243)`: the automatic detector knows the `SCPN-60160` disc serial, not a generic HDD/BIOS identity.

## Implemented in this checkpoint

- `pcsx2-qt/Settings/EmulationSettingsWidget.ui`: **EE Execution Profile** selector added to Emulation → System Settings after EE Cycle Skipping. This panel is visible without enabling PCSX2's separate Advanced Settings category. Keyboard tab stop included.
- `pcsx2-qt/Settings/EmulationSettingsWidget.cpp`: populates `QComboBox` with stable item data:
  - Automatic (Default) → `Auto`
  - Standard → `Standard`
  - Extended MMU / OS → `Extended`
- Uses existing `SettingWidgetBinder::BindWidgetToStringSetting` for `EmuCore` / `EEExecutionProfile`, with `Auto` as global default. Supports **global and per-game** settings; per-game widgets get an inherited "Use Global Setting [...]" entry automatically. Binder persists the stored item data, not potentially translated UI text.
- Adds contextual help explaining that Extended is intended for PSBBN's experimental OS/MMU path; a HDD-only boot requires explicit Extended, and **the selection is latched at VM boot**. Changing it while guest code executes does not change the active JIT profile; fully shut down the virtual PS2 and boot again.

## Verification

Source reread found matching `m_ui.eeExecutionProfile` UI/constructor references, matching binder section and key, and `VMManager::UpdateDiscDetails(booting)` still resolves the same setting after loading game settings. No changes to Standard/Extended engine semantics, detection rules, or cache invalidation.

**Unverified:** Qt uic generation, MSVC build, actual global/per-game UI interaction, selection after a full reboot, and PSBBN runtime performance. Do not claim otherwise. The user can now compile and test.

## Next steps

1. On next Windows build, open **Settings → Emulation → System Settings → EE Execution Profile**, set **Extended MMU / OS**, then **fully shut down and reboot the emulated PS2**.
2. Verify the boot log shows `EE execution profile: ExtendedMMU (setting=Extended, ...)`. Confirm this is the same boot's profile and that another ordinary game can still boot Standard.
3. Repeat controlled Linux boot and compare `projectionRebuildsSkipped`, `permissionOnlyUpdates`, `duplicatePhysicalClearsAvoided`, `neverCompiled`, actual blocks evicted and regenerated, cache resets and FPS to the previous Standard-mode log.
4. If UI selection does not propagate to `VMManager` as intended, verify per-game layer precedence in `Host::GetStringSettingValue` and any already-open VM boot latch. No performance claim until run.
