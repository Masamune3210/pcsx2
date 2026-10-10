#!/usr/bin/env python3
"""Offline static tests for the lean US Dark Cloud 2 HDD restoration."""
import importlib.util
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "DC2_US_HDD_Build_Production_20261010_v5.py"
PATCH = ROOT / "DC2_US_HDD_Production_20261010_v5.pnach"
OLD = ROOT / "DC2_US_HDD_Restoration_Clean_20261010_v4.pnach"
spec = importlib.util.spec_from_file_location("dc2_production", BUILD)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class TestProductionHDD(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = PATCH.read_text(encoding="utf-8")
        cls.entries = mod.parse(cls.text)
        cls.payload = b"".join(struct.pack("<I", cls.entries[mod.BASE+off][1])
                               for off in range(0,mod.SIZE,4))

    def test_expected_instruction_coverage(self):
        mod.audit(self.text,self.payload)
        self.assertEqual(len(self.entries),127)

    def test_rebuild_reproduces_binary_exactly(self):
        self.assertEqual(mod.load_binary(),self.payload)

    def test_no_debug_status_data(self):
        self.assertNotIn("DC2HDD",self.text)
        self.assertNotIn("EXPERIMENTAL",self.text)
        self.assertNotIn("Capacity Test",self.text)
        self.assertNotIn(b"DC2HDD",self.payload)
        self.assertEqual(len(self.payload),488)

    def test_six_api_calls_in_expected_order(self):
        mod.inspect(self.payload)

    def test_no_unexpected_additional_hook(self):
        self.assertEqual(set(self.entries)-set(range(mod.BASE,mod.BASE+mod.SIZE,4)),
                         {mod.GATE[1],*mod.HOOKS,*mod.CAPACITY})

    def test_capacity_instructions_unchanged(self):
        for a,v in mod.CAPACITY.items():
            self.assertEqual(self.entries[a],(0,v))

    def test_gate_and_trampoline_unchanged(self):
        self.assertEqual(self.entries[mod.GATE[1]],(1,0))
        for a,v in mod.HOOKS.items():
            self.assertEqual(self.entries[a],(0,v))

    def test_old_functional_hooks_match_when_v4_available(self):
        if not OLD.exists(): self.skipTest("v4 archive not supplied; constants still statically asserted")
        old={}
        for line in OLD.read_text().splitlines():
            if line.startswith("patch="):
                mode,device,a,kind,val=line.replace("patch=","",1).split(",")
                old[int(a,16)]=(int(mode),int(val,16))
        for a in [mod.GATE[1],*mod.HOOKS,*mod.CAPACITY]:
            self.assertEqual(self.entries[a],old[a])

    def test_wrong_word_rejected(self):
        corrupted=self.text.replace("patch=0,EE,01FE8000,word,27BDFF40",
                                    "patch=0,EE,01FE8000,word,27BDFF44")
        with self.assertRaises(ValueError): mod.audit(corrupted,self.payload)

    def test_missing_write_rejected(self):
        corrupted=self.text.replace("patch=0,EE,0031C4BC,word,2A42000C\n","")
        with self.assertRaisesRegex(ValueError,"missing"): mod.audit(corrupted,self.payload)

    def test_duplicate_write_rejected(self):
        with self.assertRaisesRegex(ValueError,"duplicate"):
            mod.parse(self.text+"patch=1,EE,0029F2EC,word,00000000\n")

    def test_unknown_directive_rejected(self):
        with self.assertRaisesRegex(ValueError,"unrecognized"):
            mod.parse(self.text+"patch=0,EE,0031C4BC,byte,0C\n")

    def test_more_than_one_group_rejected(self):
        with self.assertRaisesRegex(ValueError,"one HDD feature group"):
            mod.parse(self.text+"\n[Extra]\n")

    def test_mode_change_rejected(self):
        changed=self.text.replace("patch=1,EE,0029F2EC","patch=0,EE,0029F2EC")
        with self.assertRaisesRegex(ValueError,"changed"): mod.audit(changed,self.payload)


if __name__=="__main__":
    unittest.main()
