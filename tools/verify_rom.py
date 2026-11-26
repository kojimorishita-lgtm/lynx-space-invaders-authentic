#!/usr/bin/env python3
"""Verify the arcade ROM placed in rom/.

The build needs the final 2 KiB bank ($1800-$1FFF) of an
original arcade Space Invaders ROM.  Which bank it is gets
decided by content alone: tools/romsets.py lists the known
banks by SHA-256, and the bank may arrive as one 2 KiB file,
as a window inside a larger file, or as two 1 KiB halves.
File names and set names play no part.

An unknown bank -- or more than one different bank -- stops
the build with a report of what was found.  Only SHA-256
hashes are kept in romsets.py; a hash is not game data.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from romsets import BANKS, detect_bank


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    rom_dir = os.path.join(os.path.dirname(here), "rom")
    bank, bank_sha, src = detect_bank(rom_dir)
    sets = BANKS[bank_sha]["sets"]
    print(f"ROM bank identified: sha256 {bank_sha[:16]}... "
          f"(from {src}; sets: {sets}).")
