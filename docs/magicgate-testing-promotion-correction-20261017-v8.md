# MagicGate promotion correction: testing non-FF merge, runtime code only (2026-10-17, v8)

**Status: completed and verified on GitHub**.

## User requirement and correction

User explicitly wanted **functional changes only**, integrated into `testing` **without a fast-forward**. Prior promotion mistakenly used a one-parent squash commit `9984a61bc1b2d9c28a2f195e70b88e707cd8d529` and an FF ref update. It also copied the six MagicGate helper/test programs and a CI workflow that were not required to ship runtime emulator changes. User already has their own local upstream-sync script; do not offer to replace it.

The error was corrected after checking that `testing` still pointed **exactly** to `9984a61b` and had no subsequent commits. The incorrect commit was replaced using an **expected-SHA-guarded force-with-lease** ref update, not a blind force push.

## Final integration commit

`testing` HEAD: **`18bd402a0e05ee611566da571b66b711e6db071e`**, a genuine non-fast-forward **two-parent merge**.

Parents, in order:
1. Original `testing` HEAD `87002889e855ed646205211f1c7878e96893bb2d`.
2. Runtime-only side commit `a879659bf5c47285b012d3a3e465e15dbf9b54f2`.

Merge tree `e212b539279b3fab9428dc3d79001c65ba6a76e7`, identical to side commit tree. Diff compared with original `testing`: **exactly four functional emulator source paths**:
- `pcsx2/CDVD/CDVD.cpp`
- `pcsx2/CDVD/MagicGateCardAuth.h`
- `pcsx2/CDVD/MagicGateCrypto.h`
- `pcsx2/SIO/Memcard/MemoryCardProtocol.cpp`

Explicitly **not** promoted: `.github/workflows/magicgate-crypto-standalone-20261017-v1.yml`, all `tools/magicgate/*` utilities/tests, all `docs/*` research/handoff notes. Those remain accessible on this original research branch. No private keys or key files entered git.

## Verification performed

1. The candidate merge commit was inspected before promotion: parents were the original `testing` commit and the runtime-only side commit; the tree SHA matched and the comparison showed only four files.
2. `testing` ref update succeeded with `force=true, expected_sha=9984a61b`.
3. The GitHub branch was reread after update and confirmed at `18bd402a`; comparing it to original `testing` showed exactly four changed source paths and two commits ahead, none behind.

User had already confirmed Visual Studio build, PS2 memory-card recognition and formatting, authenticated HDD Browser boot, and a Dark Cloud 2 HDD-installed game session on the source branch. **No fresh build/CI on this merge commit has been performed**, and the old `9984a61b` ref was superseded. Because the old remote commit was rewritten, a local clone that already fetched the mistaken `testing` tip might need a careful local reconciliation; never discard uncommitted/local work automatically.

## Next step

Use the user's existing upstream synchronization script to fetch and integrate PCSX2 upstream. The corrected `testing` branch is the target. Preserve the source branch for ongoing MagicGate investigation and logs. No further changes to `testing` are pending in this handoff.
