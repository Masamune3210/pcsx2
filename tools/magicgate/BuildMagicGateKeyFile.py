#!/usr/bin/env python3
"""Create a BIOS-adjacent .mgkeys file from previously published MagicGate key JSON.

Never embeds or logs the supplied key values; never overwrites an existing file.
"""
import argparse
import json
import os
from pathlib import Path

REQUIRED = {
    "MG_SIG_MASTER_KEY": 8,
    "MG_SIG_HASH_KEY": 8,
    "MG_KBIT_MASTER_KEY": 16,
    "MG_KBIT_IV": 8,
    "MG_KC_MASTER_KEY": 16,
    "MG_KC_IV": 8,
    "MG_ROOTSIG_MASTER_KEY": 8,
    "MG_ROOTSIG_HASH_KEY": 16,
    "MG_CONTENT_TABLE_IV": 8,
}
OPTIONAL = {"MG_CONTENT_IV": 8}
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("public_keys_json", type=Path)
    parser.add_argument("selected_bios_path", type=Path)
    args = parser.parse_args()
    keys = json.loads(args.public_keys_json.read_text(encoding="utf-8"))
    for name, expected in {**REQUIRED, **OPTIONAL}.items():
        value = keys.get(name)
        if name in OPTIONAL and value is None:
            continue
        if not isinstance(value, str) or len(value) != expected * 2:
            raise SystemExit("Missing or wrong key length: " + name)
        try:
            bytes.fromhex(value)
        except ValueError as exc:
            raise SystemExit("Invalid key hex: " + name) from exc
    output = args.selected_bios_path.with_suffix(".mgkeys")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_BINARY"):
        flags |= os.O_BINARY
    fd = os.open(output, flags, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="ascii", newline="\n") as stream:
            stream.write("# User-supplied public PS2 KELF keys; do not add to Git.\n")
            for name in (*REQUIRED, *(name for name in OPTIONAL if name in keys)):
                stream.write(name + "=" + keys[name].upper() + "\n")
    except BaseException:
        try:
            output.unlink()
        except OSError:
            pass
        raise
    print("Created per-BIOS .mgkeys file:", output)
    print("No key values printed or committed.")
if __name__ == "__main__":
    main()
