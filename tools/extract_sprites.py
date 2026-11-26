#!/usr/bin/env python3
"""Generate the sprite/pattern data that derives from the ROM, and
emit generated/sprites_data.h.

Derived here from the builder's own ROM at build time:
  the squiggly enemy bullet pattern: ROM shot sprite at $1CD0,
      4 frames of 3 columns x 8 rows.  The port's pattern is
      3 x 5: columns map 1:1, rows take the ROM rows k6..k2
      (bit 6 .. bit 2 of each column byte).  With this rule the
      generated pattern reproduces the shipped game exactly.
  UFO explosion: ROM saucer explosion sprite at $1D7C,
      24 columns x 8 rows, downscaled to 12 x 6 by OR-ing
      column pairs and taking rows k6..k1.  (Adopted by the
      maintainer on 2026-11-24 over the earlier hand reduction.)

Port-original data embedded in this script (NOT from the ROM;
the maintainer's own work, kept here so the generated header is
complete):
  the rolling bullet pattern (the original single bullet design)
  and the plunger bullet pattern (the port's zig-zag re-drawing;
  the maintainer chose to keep it on 2026-11-24 rather than the
  mechanical conversion of the ROM plunger sprite).

The 3x5 small font is NOT generated: the maintainer chose to
keep the port's hand-shrunk glyphs, which stay in src/main.c.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from romsets import open_rom

# Port-original: rolling bullet, 4 frames of 5 rows, bits 4/2/1 =
# the three portrait columns.
ROLLING = [[0, 2, 2, 2, 2], [2, 0, 2, 2, 2],
           [2, 2, 0, 2, 2], [2, 2, 2, 0, 2]]
# Port-original: plunger bullet (zig-zag re-drawing, kept by the
# maintainer's decision of 2026-11-24).
PLUNGER = [[2, 6, 2, 3, 2], [2, 3, 2, 6, 2],
           [2, 6, 2, 6, 2], [2, 3, 2, 3, 2]]


def grid(img, addr, ncols):
    """8 rows, top (bit 7) first, each a list of 0/1 per column."""
    return [[(img[addr + c] >> k) & 1 for c in range(ncols)]
            for k in range(7, -1, -1)]


def shot_pattern(img, addr):
    """4 frames of 3x5 (rows of 3-bit values, bits 4/2/1 = columns),
    taking ROM rows k6..k2 (grid rows 1..5)."""
    frames = []
    for f in range(4):
        g = grid(img, addr + f * 3, 3)
        frames.append([sum(g[r][c] << (2 - c) for c in range(3))
                       for r in range(1, 6)])
    return frames


def suzy_pack(pattern):
    """Pack a [4][5] pattern into Suzy literal records: per column
    {0x03, byte, 0x00} with bit 7 = top row, plus a 0x00 terminator."""
    out = []
    for frame in pattern:
        rec = []
        for c in range(3):
            b = 0
            for r in range(5):
                if (frame[r] >> (2 - c)) & 1:
                    b |= 1 << (7 - r)
            rec += [0x03, b, 0x00]
        rec.append(0x00)
        out.append(rec)
    return out


def runs_of(bitmap):
    """[row][x] 0/1 grid -> list of (row, start, length) runs."""
    runs = []
    for r, row in enumerate(bitmap):
        x = 0
        while x < len(row):
            if row[x]:
                s = x
                while x < len(row) and row[x]:
                    x += 1
                runs.append((r, s, x - s))
            else:
                x += 1
    return runs


def fmt_frames(frames):
    return ",\n".join(
        "    {" + ",".join("{" + ",".join(str(v) for v in fr) + "}"
                           for fr in frames) + "}"
        for frames in [frames])


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    img, offsets, bank_sha, bank_src = open_rom(os.path.join(root, "rom"))

    squiggly = shot_pattern(img, offsets["squiggly"])
    bomb_bits = [ROLLING, PLUNGER, squiggly]
    bomb_spr = [suzy_pack(p) for p in bomb_bits]

    # UFO explosion: 24x8 -> 12x6, column pairs OR-ed, rows k6..k1.
    exp = grid(img, offsets["saucer_exp"], 24)
    exp12 = [[exp[r][c] | exp[r][c + 1] for c in range(0, 24, 2)]
             for r in range(1, 7)]
    exp_runs = runs_of(exp12)

    lines = []
    lines.append("/* GENERATED from the builder's own ROM by "
                 "tools/extract_sprites.py.")
    lines.append(" * Do not edit; do not commit (this file is gitignored).")
    lines.append(f" * Source bank SHA-256: {bank_sha}")
    lines.append(f" * (identified in rom/ as: {bank_src}). */")
    lines.append("")
    lines.append("/* Enemy bullet patterns: 3 types x 4 frames x 5 rows,")
    lines.append(" * bits 4/2/1 = the three portrait columns.")
    lines.append(" * Types 0 (rolling) and 1 (plunger) are the port's own")
    lines.append(" * designs; type 2 (squiggly) is converted from the ROM")
    lines.append(" * shot sprite ($1CD0, rows k6..k2). */")
    lines.append("static const unsigned char bomb_bits[3][4][5]={")
    for t, frames in enumerate(bomb_bits):
        lines.append("    {" + ",".join(
            "{" + ",".join(str(v) for v in fr) + "}" for fr in frames) +
            "}" + ("," if t < 2 else ""))
    lines.append("};")
    lines.append("")
    lines.append("/* The same patterns as Suzy sprite literals (one record")
    lines.append(" * per portrait column, bit 7 = the top row). */")
    lines.append("static unsigned char bomb_spr[3][4][10]={")
    for t, frames in enumerate(bomb_spr):
        lines.append("    {" + ",".join(
            "{" + ",".join(f"0x{b:02X}" for b in rec) + "}"
            for rec in frames) + "}" + ("," if t < 2 else ""))
    lines.append("};")
    lines.append("")
    lines.append("/* UFO explosion runs {row, x, length} in a 12x6 box,")
    lines.append(" * converted from the ROM saucer explosion sprite")
    lines.append(" * ($1D7C): column pairs OR-ed, rows k6..k1. */")
    lines.append("#define UFO_EXPLOSION_RUN_COUNT %d" % len(exp_runs))
    lines.append("static const unsigned char ufo_explosion_runs[%d][3]={" %
                 len(exp_runs))
    body = ",".join("{%d,%d,%d}" % r for r in exp_runs)
    lines.append("    " + body + "};")
    lines.append("")

    out = os.path.join(root, "generated", "sprites_data.h")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
