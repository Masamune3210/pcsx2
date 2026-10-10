# DuckStation-inspired memory card editor for PCSX2

Status: **research parked; no implementation yet**  
Branch: `research/duckstation-style-memory-card-editor` (from `master`)  
Goal: integrate a convenient, first-party **PS2** save/memory-card editor into PCSX2's desktop UI, taking **UX inspiration** from DuckStation's PS1 memory-card editor.

## Why

DuckStation offers a two-card editor that makes it straightforward to inspect saves and move/copy them between cards. PCSX2 supports memory-card creation, assignment and both file/folder cards, but the user wants a similarly integrated **per-save management UI** rather than leaving save editing to external tools.

This is **not** a request to transplant DuckStation's PS1 card formats or assume its source is licensed for incorporation. Study its interaction design and implement PS2-aware equivalents, respecting licensing.

## Reference implementations and research starting points

- DuckStation UI: https://github.com/stenzek/duckstation/blob/master/src/duckstation-qt/memorycardeditorwindow.cpp
- DuckStation UI header: https://github.com/stenzek/duckstation/blob/master/src/duckstation-qt/memorycardeditorwindow.h
- DuckStation core PS1 memory card format: https://github.com/stenzek/duckstation/blob/master/src/core/memory_card_image.cpp
- PCSX2 official memory-card documentation: https://pcsx2.net/docs/configuration/memcards/
- **PCSX2/myMCpp** (Qt/C++ PS2 card and save manager, experimental alpha): https://github.com/PCSX2/myMCpp

DuckStation's UI header names these operations: load/create cards A and B; view their files and free blocks; save/format; import individual saves or whole cards; export/rename/delete/undelete; copy saves between cards; extract static/animated icons. The *idea* is the reference, not a PS1-specific implementation to port.

myMCpp documents opening .ps2/.mc2/.mcd/.vm2/raw VMC images, listing save files and importing/exporting/deleting, along with filesystem/ECC checks. It is GPLv3; review project licenses, dependencies and design compatibility before considering code reuse.

## Desired behavior / preliminary scope

- Open/select two memory cards side-by-side (or another comparable arrangement), inspect game-save entries and available capacity.
- Copy saves between cards with collision checks; export and import individual saves in appropriate **PS2** formats and preserve save metadata.
- Delete with explicit confirmation; use backups and safe writes; consider rename/restore/undelete only where PS2 filesystem semantics genuinely allow safe support.
- Optional PS2 save icon/title preview and icon extraction if format/parsing support exists.
- Integrate from existing PCSX2 memory-card management UI; continue supporting ordinary file-based cards and folder cards (whose per-game isolation semantics differ).
- Handle cards active in emulation safely: ideally disallow writes while in use, or require a controlled detach/reload path. Never silently edit a mounted card.

## Technical questions to resolve before coding

1. **Inventory existing PCSX2 code:** find memory-card settings UI and file/folder card implementations, existing persistence/locking and data structures. Avoid duplicating capabilities.
2. **Evaluate myMCpp:** its actual supported save formats and error/ECC/metadata handling, integration cost, project maturity and license fit. Prefer reusing a PS2-native API/library when viable.
3. **Design atomicity:** use a copied working image and verify before replacement, with automatic backups, error reporting, full-card checks, and recovery behavior.
4. **Map formats:** support raw PS2 cards (8/16/32/64 MB if existing PCSX2 supports them), folder cards, and common PS2 save imports/exports where there is proven parser support. Distinguish unsupported formats explicitly.
5. **Test real save structures:** multi-file saves and directories, Japanese Shift-JIS names, duplicates, deleted entries, full/fragmented cards, ECC corruption, in-use cards, and cross-slot copying.
6. **Licensing:** DuckStation's current source files have an SPDX `CC-BY-NC-ND-4.0` header. **Do not copy or adapt code** from them into PCSX2 without an appropriate license grant. Using the general workflow concept is different from copying implementation.

## Proposed first increment

Build a **read-only** PCSX2 GUI window showing a selected card's directory of saves, name/size/icon metadata (when available), and free space. Validate against real file and folder cards before introducing any write operations. Then add safe import/export and card-to-card copying.

## Current state and next step

- Tracking branch and this document only.
- No UI implementation, parser changes, or integration tests performed.
- **Resume at:** inventory PCSX2's current memcard GUI and storage APIs and inspect PCSX2/myMCpp for feasible backend reuse. Record concrete files/functions and update this document before developing the UI.

Keep this branch independent of `research/dark-cloud-2-us-hdd-restoration`; the two investigations concern unrelated features.
