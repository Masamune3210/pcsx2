#!/usr/bin/env python3
"""Standard-library, offline regression suite for the known-good US HDD patch.

Run: python -m unittest -v DC2_US_HDD_Restoration_Tests_20261010_v4.py
"""
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parent
TOOL = ROOT / 'DC2_US_HDD_Restoration_Packager_20261010_v4.py'
PATCH = ROOT / 'DC2_US_HDD_Restoration_Clean_20261010_v4.pnach'
spec = importlib.util.spec_from_file_location('dc2_hdd_v4', TOOL)
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class RestorationPackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = PATCH.read_text(encoding='utf-8')
        cls.entries = pack.parse_pnach(cls.text)
        cls.blob = b''.join(struct.pack('<I', cls.entries[pack.BASE + i][1]) for i in range(0, pack.PAYLOAD_SIZE, 4))

    def test_known_good_payload_and_all_directives(self):
        pack.audit_text(self.text, self.blob, 'unit test')
        self.assertEqual(len(self.entries), 189)

    def test_exact_bytecode_matches_previous_release(self):
        prior = ROOT / 'DC2_US_HDD_Restoration_20261010_v3.pnach'
        if not prior.exists():
            self.skipTest('Previous v3 .pnach not present; SHA-256 still enforced by test above')
        self.assertEqual(self.entries, pack.parse_pnach(prior.read_text()))

    def test_duplicate_address_rejected(self):
        with self.assertRaisesRegex(ValueError, 'duplicate EE address'):
            pack.parse_pnach(self.text + 'patch=0,EE,0031C4BC,word,2A42000C\n')

    def test_wrong_capacity_immediate_rejected(self):
        bad = self.text.replace('patch=0,EE,0031C4BC,word,2A42000C', 'patch=0,EE,0031C4BC,word,2A42000B')
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            pack.audit_text(bad, self.blob, 'negative')

    def test_wrong_loader_word_rejected(self):
        bad = self.text.replace('patch=0,EE,01FE8000,word,27BDFF40', 'patch=0,EE,01FE8000,word,27BDFF41')
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            pack.audit_text(bad, self.blob, 'negative')

    def test_malformed_patch_not_silently_ignored(self):
        with self.assertRaisesRegex(ValueError, 'malformed/unsupported'):
            pack.parse_pnach(self.text + 'patch=0,EE,0031C4BC,byte,0C\n')

    def test_unknown_directive_not_silently_ignored(self):
        with self.assertRaisesRegex(ValueError, 'unexpected'):
            pack.parse_pnach(self.text + 'include = unknown.pnach\n')

    def test_group_names_remain_stable(self):
        bad = self.text.replace('[Dark Cloud 2 - EXPERIMENTAL HDD Driver Loader]', '[Dark Cloud 2 - HDD Driver Loader]')
        with self.assertRaisesRegex(ValueError, 'group names'):
            pack.parse_pnach(bad)

    def test_missing_capacity_patch_rejected(self):
        bad = self.text.replace('patch=0,EE,0031BF40,word,2A220680\n', '')
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            pack.audit_text(bad, self.blob, 'negative')

    def test_mode_change_rejected(self):
        bad = self.text.replace('patch=1,EE,0029F2EC,word,00000000', 'patch=0,EE,0029F2EC,word,00000000')
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            pack.audit_text(bad, self.blob, 'negative')

    def test_synthetic_activation_log(self):
        lines = []
        for group in ('Dark Cloud 2 - Existing HDD Title Menu Gate',
                      'Dark Cloud 2 - EXPERIMENTAL HDD Driver Loader',
                      'Dark Cloud 2 - US HDD Installer 1664 MiB Capacity Test'):
            lines.append(f'[   12.0000] Enabled patch: {group}')
            for addr, (mode, value) in self.entries.items():
                if (group.endswith('Gate') and addr == pack.GATE[1]) or (
                        group.endswith('Loader') and (pack.BASE <= addr < pack.BASE + pack.PAYLOAD_SIZE or addr in pack.HOOKS)) or (
                        group.endswith('Test') and addr in pack.CAPACITY_IMMEDIATES):
                    lines.append(f'[   12.0001]   {mode},EE,word,{addr:08x},{value:x}')
        pack.check_log('\n'.join(lines), pack.expected(self.blob))
        with self.assertRaisesRegex(ValueError, 'missing'):
            pack.check_log('\n'.join(x for x in lines if '0031c4bc' not in x), pack.expected(self.blob))
        with self.assertRaisesRegex(ValueError, 'Conflicting EE write'):
            pack.check_log('\n'.join(lines + ['[   13.0000] 0,EE,word,0031c4bc,2a42000b']),
                           pack.expected(self.blob))


if __name__ == '__main__':
    unittest.main()
