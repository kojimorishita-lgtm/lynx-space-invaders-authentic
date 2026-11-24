# Space Invaders for Atari Lynx — a fan-made port

[日本語版はこちら](README.ja.md)

An unofficial, fan-made port of the 1978 arcade classic to the
Atari Lynx, written in C (cc65). Not affiliated with or endorsed
by TAITO.

What I have valued most in this project is keeping the
atmosphere of the arcade original exactly as it was. I set
out to realize that on the ATARI LYNX — a machine often
thought to have little expressive power — and to create
something that has never existed before. Every design
decision in this port followed that policy.

## What you need

- Your own copy of the ROM set of the original arcade
  board (TAITO/Midway, 1978): four 2 KiB files (see
  "Build" below for which files). **This repository
  contains no arcade data at all.** The ROM is verified
  by SHA-256 at build time and the data the game needs
  is generated from it on your machine; without the
  right ROM the build stops.
- A Linux environment with cc65 2.19, Python 3 and GNU
  make. Developed and tested on Ubuntu 24.04; the
  maintainer builds on Windows 11 with WSL2 (Ubuntu).
  On Ubuntu (including WSL), the compiler and the sound
  converter install with: `sudo apt install cc65 ffmpeg`
- For sound: ten WAV files in `sounds/` (see "Sound" below)
  and ffmpeg. Without them the game builds and plays
  silently.

No prebuilt `.lnx` is distributed: a built image necessarily
contains data generated from the ROM.

## Build

If you have the source as a .zip archive, it extracts into
a folder named `lynx-space-invaders-authentic/`. Do all of
the following inside that folder.

1. Place the four ROM files of the original arcade board
   in `rom/`. They are the game's program ROMs, one file
   per 2 KiB bank:

   | file         | address range | SHA-256 (first 16) |
   |--------------|---------------|--------------------|
   | `invaders.h` | $0000 – $07FF | `1dfb19b1eeeac5b9` |
   | `invaders.g` | $0800 – $0FFF | `ac7031b046274d4c` |
   | `invaders.f` | $1000 – $17FF | `5c8e573ebde9a673` |
   | `invaders.e` | $1800 – $1FFF | `0a97ed62e2063c13` |

   If you are not sure your files are the right ones, just
   run the build: the first step compares the full SHA-256
   of each file (the full hashes are in
   `tools/verify_rom.py`) and stops, naming the file that
   does not match.

2. Optional, for sound: create a `sounds/` folder next
   to `rom/` and put the ten WAV files listed under
   "Sound" below into it. Without them the game builds
   silently.

3. Build:

   ```sh
   make            # -> out/space-invaders-authentic.lnx
   ```

`make` runs, in order: ROM verification
(`tools/verify_rom.py`), asset generation
(`tools/extract_assets.py`, `tools/extract_sprites.py`),
sound generation (`tools/gen_sound_data.py`), and the cc65
compile of `src/`.

## What is faithful, what is adapted

Strict imitations of the arcade original:

- The formation steps one alien at a time, so the motion
  travels through the ranks like a wave — never block movement.
- Attract mode: title, score-advance table, demo play, credit
  screen — including the two sight gags of the original.
- The `PLAY PLAYER<1>` prompt with its ~3 second wait, and
  the formation dealt in one alien at a time at game start
  and on every new rack.
- Three shot types with the arcade's firing rules: the
  rolling shot aims at the player, the plunger follows the
  ROM's column table, the squiggly shares its slot with the
  saucer; the reload rate tightens as the score rises.
- The mystery-score table, the march tempo that accelerates
  as aliens fall, destructible shields, EXTEND at 1500.

Adapted for the Lynx (deliberate, documented in the source):

- All graphics are redrawn for the Lynx screen: the alien
  and cannon sprites and the small font are hand-drawn
  imitations; a few patterns (font, tables, the squiggly
  shot, the saucer explosion) are converted from your ROM
  at build time.
- The demo is capped (1200 updates) so the attract loop
  keeps cycling; the saucer's direction alternates instead
  of using the ROM's byte lottery.
- Fire input is buffered shortly before the cannon can fire
  again when the formation is about to land.

## Controls

- Left / Right: move the cannon
- A: shoot
- Any button on the title screen: start

## Verified environments

The final build has been run and checked, picture and
sound, on:

- Real hardware (Atari Lynx I)
- Handy 0.95 (Windows)
- The Handy core for libretro / RetroArch
- Mednafen
- MAME (Lynx driver)

## Sound

The sound engine (a Timer-7 interrupt DAC with IMA-ADPCM and
raw playback) is part of this repository. The samples
themselves are not: they were sampled from the arcade game.
If you place WAV files in `sounds/` with the names below,
the game is built with sound. Any sample rate works — the
files are converted at build time.

- `shoot.wav` — the cannon's shot: the laser sound when the
  player fires.
- `killed.wav` — an invader destroyed: the short burst when
  an alien is hit.
- `explosion.wav` — the player's cannon destroyed: the
  explosion when the player is hit.
- `extend.wav` — the extra-life award at 1500 points: a
  steady tone of about one second.
- `march1.wav` … `march4.wav` — the four bass notes of the
  invaders' march, played in turn (1, 2, 3, 4) as the
  formation steps; the tempo rises as the aliens fall.
- `ufo.wav` — the saucer in flight: a warbling tone, looped
  while it crosses the screen. The file should hold one
  full cycle of the warble.
- `ufohit.wav` — the saucer destroyed: the long warbling
  sound that rings out once (about two seconds).

You do not need all ten: the build uses the files you
provide, and any sound whose file is missing stays silent.
With no `sounds/` at all, the game is fully silent.

## Future plans

- Reproduce the arcade bug seen when an octopus is the
  last invader remaining: part of it is left unerased,
  trailing like an aurora.
- Look for a way to keep a walking crab from visually
  merging with its neighbour, without changing its design.
- Improve the choppy sound playback on Handy 0.95
  (Windows).

## Credits and rights

- The original game: SPACE INVADERS © TAITO CORPORATION.
- This port — its code, its Lynx-specific design and its
  build tools — © 2026 Koji Morishita, released under the
  MIT license (see [LICENSE](LICENSE)). The license covers
  only the port's own work; it grants no rights in the
  original game.

## Disclaimer

This is a non-commercial fan work made for study and
preservation. It is not affiliated with TAITO, and no arcade
data is included in this repository.
