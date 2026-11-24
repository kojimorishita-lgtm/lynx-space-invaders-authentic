# Build the Lynx port from source.
#
# This repository contains no arcade data.  The build:
#   1. verifies the builder's own ROM set in rom/ (SHA-256),
#   2. generates the ROM-derived tables and patterns into
#      generated/ (tools/extract_assets.py, extract_sprites.py),
#   3. generates the sound data (tools/gen_sound_data.py) --
#      from sounds/ if the builder placed the WAV files there,
#      or silent data of the same lengths otherwise,
#   4. compiles src/ with cc65 into out/.
#
# Without the right ROM in rom/ the build stops at step 1.
# See README.md for details.

PY = python3
ALIENS = 55
TARGET = invaders_v45_solid_cache1_integration_count$(ALIENS).lnx
OUT = space-invaders-authentic.lnx

all: verify generate out/$(OUT)

verify:
	$(PY) tools/verify_rom.py

generate:
	$(PY) tools/extract_assets.py
	$(PY) tools/extract_sprites.py
	$(PY) tools/gen_sound_data.py

out/$(OUT): src/main.c src/Makefile
	rm -rf build
	mkdir -p build out
	cp src/* build/
	rm -f build/sound_data.asm build/vpdiff_table.asm \
	      build/gen_sound_data.py build/*.lnx build/*.o
	cp generated/sound_data.asm generated/vpdiff_table.asm build/
	$(MAKE) -C build V45_START_ALIENS=$(ALIENS)
	cp build/$(TARGET) out/$(OUT)

clean:
	rm -rf build out generated

.PHONY: all verify generate clean
