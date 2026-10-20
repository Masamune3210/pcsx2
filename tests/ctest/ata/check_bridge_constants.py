#!/usr/bin/env python3
"""Check the isolated bridge's copied register constants without compiling."""

from pathlib import Path
import re


def definitions(path):
    found = {}
    for line in path.read_text().splitlines():
        match = re.match(r"\s*#\s*define\s+(\w+)\s+(.+)", line)
        if match:
            found[match[1]] = re.sub(r"\s+", "", match[2].split("//", 1)[0])
    return found


here = Path(__file__).resolve().parent
production = definitions(here.parents[2] / "pcsx2/DEV9/DEV9.h")
bridge = definitions(here / "bridge/DEV9/DEV9.h")
for name, value in bridge.items():
    if production.get(name) != value:
        raise SystemExit(f"{name}: bridge {value!r} differs from production {production.get(name)!r}")
print(f"Matched {len(bridge)} bridge constants to production DEV9.h (source comparison only).")
