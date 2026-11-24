#!/usr/bin/env python3
"""Verify the original arcade ROM set placed in rom/.

The build needs the four 2 KiB ROMs of the original arcade
board (the "invaders" set, TAITO/MIDWAY, 1978), mapped as:
    invaders.h -> $0000, invaders.g -> $0800,
    invaders.f -> $1000, invaders.e -> $1800
Only SHA-256 hashes are kept here -- a hash is not game data.
A mismatch stops the build: no assets can be generated without
the right ROM.
"""
import hashlib
import os
import sys

EXPECTED = {
    "invaders.h": "1dfb19b1eeeac5b91ff4861ab331e183500785a3c958a6fa30aac0f37a7bbc65",
    "invaders.g": "ac7031b046274d4cd963cebf02f6e0ea50104e2a29681c387b1cb27e1e3e343d",
    "invaders.f": "5c8e573ebde9a673d4e7809d3f36aaa20d9062f3616d025d85c3f04fcfe11d72",
    "invaders.e": "0a97ed62e2063c13e373a9a61ea3d8fbb701e414af05953e16f823fd879d5f49",
}
ORDER = ("invaders.h", "invaders.g", "invaders.f", "invaders.e")


def load_image(rom_dir):
    """Return the 8 KiB image, or exit with a clear message."""
    parts = []
    for name in ORDER:
        path = os.path.join(rom_dir, name)
        if not os.path.isfile(path):
            print(f"error: {path} not found.")
            print("Place the four ROM files of the 'invaders' set in rom/:")
            print("  invaders.h, invaders.g, invaders.f, invaders.e (2 KiB each)")
            sys.exit(1)
        data = open(path, "rb").read()
        digest = hashlib.sha256(data).hexdigest()
        if len(data) != 2048 or digest != EXPECTED[name]:
            print(f"error: {name} does not match the expected ROM.")
            print(f"  size:   {len(data)} (expected 2048)")
            print(f"  sha256: {digest}")
            print(f"  wanted: {EXPECTED[name]}")
            print("The build needs the original 'invaders' set; other")
            print("revisions or clone sets will not work.")
            sys.exit(1)
        parts.append(data)
    return b"".join(parts)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    rom_dir = os.path.join(os.path.dirname(here), "rom")
    load_image(rom_dir)
    print("ROM set verified: invaders (h/g/f/e), 8 KiB image OK.")
