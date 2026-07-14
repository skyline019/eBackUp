#!/usr/bin/env python3
"""Verify Python init_keyed_gear_table matches C++ InitKeyedGearTable."""

from __future__ import annotations

import json
import sys
from pathlib import Path

from common import init_keyed_gear_table

FIXTURE = Path(__file__).parent / "fixtures" / "keyed_gear_12345678.json"


def main() -> int:
    seed = 0x12345678
    table = init_keyed_gear_table(seed)
    if not FIXTURE.exists():
        FIXTURE.parent.mkdir(parents=True, exist_ok=True)
        FIXTURE.write_text(json.dumps(table, indent=2), encoding="utf-8")
        print(f"Wrote fixture {FIXTURE} (run C++ GearParityTest to verify)")
        return 0

    golden = json.loads(FIXTURE.read_text(encoding="utf-8"))
    mismatches = []
    for i, (a, b) in enumerate(zip(table, golden)):
        if a != b:
            mismatches.append((i, a, b))
    if mismatches:
        print(f"FAIL: {len(mismatches)} gear table mismatches")
        for i, a, b in mismatches[:10]:
            print(f"  [{i}] py={a:#010x} cpp={b:#010x}")
        return 1
    print(f"PASS: gear table parity seed={seed:#x} ({len(table)} entries)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
