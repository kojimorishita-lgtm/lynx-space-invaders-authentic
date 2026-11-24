#!/usr/bin/env python3
"""Extract the ROM-derived data tables from the verified ROM image
and emit generated/rom_data.h.

Everything in the output comes from the builder's own ROM at build
time; the repository itself contains none of these bytes.

Covered here (plain data tables):
  font glyphs        $1E00, 42 glyphs x 8 bytes
  UFO mystery points $1D54, 16 BCD entries (the game uses 15)
  demo commands      $1F74, 10 bytes
  column fire table  $1D00, 32 bytes
  march thresholds   $1A11, 16 bytes
  march delays       $1A21, 16 bytes
  shot reload rates  $1AA1, 5 bytes, selected by the BCD score
                     thresholds at $1CB8 (4 bytes)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from verify_rom import load_image


def fmt_bytes(vals, per=8, indent="    "):
    out = []
    for i in range(0, len(vals), per):
        out.append(indent + ",".join(str(v) for v in vals[i:i + per]) +
                   ("," if i + per < len(vals) else ""))
    return "\n".join(out)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    img = load_image(os.path.join(root, "rom"))

    font = [img[0x1E00 + i * 8:0x1E00 + i * 8 + 8] for i in range(42)]
    ufo_bcd = img[0x1D54:0x1D54 + 16]
    ufo_points = [((b >> 4) * 10 + (b & 0x0F)) * 10 for b in ufo_bcd[:15]]
    demo_cmds = list(img[0x1F74:0x1F74 + 10])
    col_fire = list(img[0x1D00:0x1D00 + 32])
    march_thr = list(img[0x1A11:0x1A11 + 16])
    march_del = list(img[0x1A21:0x1A21 + 16])
    reload_tab = list(img[0x1CB8:0x1CB8 + 4])
    reload_rates = list(img[0x1AA1:0x1AA1 + 5])
    # The arcade compares the BCD high byte of the score against
    # reload_tab; the port compares the integer score, so a BCD
    # threshold byte B becomes the integer bound bcd(B)*100+99
    # (0x02 -> 299, 0x10 -> 1099, 0x20 -> 2099, 0x30 -> 3099).
    reload_bounds = [((b >> 4) * 10 + (b & 0x0F)) * 100 + 99
                     for b in reload_tab]

    lines = []
    lines.append("/* GENERATED from the builder's own ROM by "
                 "tools/extract_assets.py.")
    lines.append(" * Do not edit; do not commit (this file is gitignored). */")
    lines.append("")
    lines.append("/* Arcade font, ROM $1E00: 42 glyphs x 8 bytes.")
    lines.append(" * byte = column (left to right), bit 0 = bottom row.")
    lines.append(" * Codes: 0-25 A-Z, 26-35 0-9, 36 <, 37 >, 38 space,")
    lines.append(" * 39 =, 40 *, 41 ?. */")
    lines.append("static const unsigned char score_font[42][8] = {")
    for i, g in enumerate(font):
        lines.append("    { " + ", ".join(f"0x{b:02X}" for b in g) +
                     " }" + ("," if i < 41 else "") + f"  /* {i} */")
    lines.append("};")
    lines.append("")
    lines.append("/* UFO mystery score table, ROM $1D54 (BCD x 10).")
    lines.append(" * The game cycles through 15 entries (arcade wrap). */")
    lines.append("static const unsigned int ufo_points[15]={")
    lines.append(fmt_bytes(ufo_points) + "};")
    lines.append("")
    lines.append("/* Demo playback commands, ROM $1F74: bit0 right, bit1 left. */")
    lines.append("static const unsigned char demo_cmds[10]={")
    lines.append(fmt_bytes(demo_cmds) + "};")
    lines.append("")
    lines.append("/* Column firing order table, ROM $1D00 (1-based columns). */")
    lines.append("static const unsigned char col_fire_table[32]={")
    lines.append(fmt_bytes(col_fire) + "};")
    lines.append("")
    lines.append("/* Fleet march: enemy-count thresholds, ROM $1A11, and")
    lines.append(" * the matching delay table, ROM $1A21. */")
    lines.append("static const unsigned char march_thresholds[16]={")
    lines.append(fmt_bytes(march_thr) + "};")
    lines.append("static const unsigned char march_delays[16]={")
    lines.append(fmt_bytes(march_del) + "};")
    lines.append("")
    lines.append("/* First march delay (ROM $1A21[0]), as a macro: main.c")
    lines.append(" * uses it inside a static initializer. */")
    lines.append(f"#define ROM_MARCH_DELAY_FIRST {march_del[0]}")
    lines.append("")
    lines.append("/* Enemy shot reload rate: rates from ROM $1AA1, chosen")
    lines.append(" * by the score thresholds at ROM $1CB8.  The arcade")
    lines.append(" * compares the BCD high byte of the score; the port")
    lines.append(" * compares the integer score, so the thresholds are")
    lines.append(" * emitted as integer bounds (0x02 -> 299, 0x10 -> 1099,")
    lines.append(" * 0x20 -> 2099, 0x30 -> 3099). */")
    lines.append("static const unsigned int reload_bounds[4]={")
    lines.append(fmt_bytes(reload_bounds, per=4) + "};")
    lines.append("static const unsigned char reload_rates[5]={")
    lines.append(fmt_bytes(reload_rates, per=5) + "};")
    lines.append("")

    out = os.path.join(root, "generated", "rom_data.h")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
