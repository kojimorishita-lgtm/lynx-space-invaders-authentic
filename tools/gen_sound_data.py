#!/usr/bin/env python3
# Generate generated/sound_data.asm (and vpdiff_table.asm) for the
# build.
#
# Sound slot: if the builder has placed WAV files into sounds/
# (gitignored -- sampled sound data is not part of this
# repository), the game is built with those sounds: each WAV is
# converted to the sample format of the port's playback engine
# (IMA-ADPCM for the one-shots with high-frequency content,
# raw 8-bit for the march notes, the UFO loop and the extend
# tone; rates follow the Timer-7 settings in src/sndirq.asm).
#
# Sounds whose WAV is missing are generated from silence with
# exactly the engine's sample lengths, so any subset works:
# all ten files -> full sound, none -> a fully silent game.
#
# Requires ffmpeg, but only for files actually present.
import os
import subprocess
import sys
import wave
import array

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SOUNDS = os.path.join(ROOT, "sounds")
OUTDIR = os.path.join(ROOT, "generated")
TMP = os.path.join(OUTDIR, ".sndtmp")

SOUND_FILES = ["shoot.wav", "killed.wav", "explosion.wav",
               "extend.wav", "march1.wav", "march2.wav",
               "march3.wav", "march4.wav", "ufo.wav", "ufohit.wav"]

# Sample lengths of the playback engine's table (snd_len in
# src/sndirq.asm).  The silent build must match them exactly.
ENGINE_LENGTHS = [("shoot", 1635, True), ("killed", 1862, True),
                  ("explosion", 4330, True), ("extend", 2623, False),
                  ("march1", 245, False), ("march2", 260, False),
                  ("march3", 263, False), ("march4", 274, False),
                  ("ufo", 898, False), ("ufohit", 5632, True)]

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
        143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
        449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282,
        1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
        3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
        9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
        22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


def adpcm_encode(samples):
    """samples: unsigned 8-bit list. Returns packed nibble bytes."""
    pred = 0
    index = 0
    out = []
    nibbles = []
    for s in samples:
        target = (s - 128) << 8
        step = STEP[index]
        diff = target - pred
        nib = 0
        if diff < 0:
            nib = 8
            diff = -diff
        vpdiff = step >> 3
        if diff >= step:
            nib |= 4
            diff -= step
            vpdiff += step
        if diff >= step >> 1:
            nib |= 2
            diff -= step >> 1
            vpdiff += step >> 1
        if diff >= step >> 2:
            nib |= 1
            vpdiff += step >> 2
        pred = pred - vpdiff if nib & 8 else pred + vpdiff
        pred = max(-32768, min(32767, pred))
        index = max(0, min(88, index + IDX[nib & 7]))
        nibbles.append(nib)
    for i in range(0, len(nibbles), 2):
        lo = nibbles[i]
        hi = nibbles[i + 1] if i + 1 < len(nibbles) else 0
        out.append(lo | (hi << 4))
    return out


def adpcm_decode(data, count):
    pred = 0
    index = 0
    out = []
    for i in range(count):
        byte = data[i >> 1]
        nib = (byte & 0x0F) if i % 2 == 0 else (byte >> 4)
        step = STEP[index]
        vpdiff = (step >> 3) + (step if nib & 4 else 0) + \
                 ((step >> 1) if nib & 2 else 0) + \
                 ((step >> 2) if nib & 1 else 0)
        pred = pred - vpdiff if nib & 8 else pred + vpdiff
        pred = max(-32768, min(32767, pred))
        index = max(0, min(88, index + IDX[nib & 7]))
        out.append((pred >> 8) + 128)
    return out


def resample(src, rate, dst):
    subprocess.run(['ffmpeg', '-y', '-v', 'error', '-i', src, '-ac', '1',
                    '-ar', str(rate), '-acodec', 'pcm_u8', dst], check=True)
    w = wave.open(dst)
    a = array.array('B')
    a.frombytes(w.readframes(w.getnframes()))
    return list(a)


def trim(a, head=True, tail=True, thr=6):
    loud = [i for i, x in enumerate(a) if abs(x - 128) > thr]
    if not loud:
        return a
    lo = loud[0] if head else 0
    hi = (loud[-1] + 1) if tail else len(a)
    return a[lo:hi]


def fade_tail(a, n=48):
    if len(a) > n and abs(a[-1] - 128) > 4:
        for i in range(n):
            k = len(a) - n + i
            a[k] = 128 + (a[k] - 128) * (n - i) // n
    return a


def snr(orig, dec):
    import math
    num = sum((o - 128) ** 2 for o in orig)
    den = sum((o - d) ** 2 for o, d in zip(orig, dec))
    return 10 * math.log10(num / den) if den else 99.0


def gain(a, g):
    out = []
    clipped = 0
    for x in a:
        v = int(round((x - 128) * g))
        if v > 127:
            v = 127
            clipped += 1
        if v < -128:
            v = -128
            clipped += 1
        out.append(128 + v)
    print('  gain %.2f: %d/%d clipped (%.1f%%)' %
          (g, clipped, len(a), 100.0 * clipped / max(1, len(a))))
    return out


def plan_from_sounds():
    """The conversion settings of the shipped game (v50 r6 / v51):
    gains and trims chosen by ear on the real machine.  A sound
    whose WAV is missing falls back to silence of the engine's
    length, and is reported."""
    def p(name):
        return os.path.join(SOUNDS, name)
    silent = dict((n, (ln, enc)) for n, ln, enc in ENGINE_LENGTHS)
    plan = []
    missing = []

    def add(name, build):
        if os.path.isfile(p(name + '.wav')):
            data, enc = build()
            plan.append((name, data, enc))
        else:
            ln, enc = silent[name]
            plan.append((name, [128] * ln, enc))
            missing.append(name)

    def b_shoot():
        return gain(fade_tail(trim(resample(
            p('shoot.wav'), 5495, f'{TMP}/shoot.wav'))), 1.35), True
    add('shoot', b_shoot)

    def b_killed():
        return gain(fade_tail(trim(resample(
            p('killed.wav'), 5495, f'{TMP}/killed.wav'))), 1.2), True
    add('killed', b_killed)

    def b_explosion():
        return gain(trim(resample(p('explosion.wav'), 5495,
                                  f'{TMP}/explosion.wav'),
                         head=False), 1.2), True
    add('explosion', b_explosion)

    # The extend sound is a near-pure 480 Hz tone: raw 8-bit at
    # 2747 Hz (Timer-7 clock select 2) beats ADPCM at the same size.
    def b_extend():
        return gain(trim(resample(p('extend.wav'), 2747,
                                  f'{TMP}/extend.wav'),
                         head=False, tail=False), 1.35), False
    add('extend', b_extend)

    # The march notes and the UFO loop are low-frequency sounds;
    # at 2747 Hz both their data and their IRQ rate halve.  The
    # one-shots keep 5495 Hz ADPCM so their edge is preserved.
    for i in range(1, 5):
        def b_march(i=i):
            return trim(resample(p(f'march{i}.wav'), 2747,
                                 f'{TMP}/march{i}.wav'),
                        head=False, tail=False), False
        add(f'march{i}', b_march)

    # The flight sound is ufo.wav looped whole: the file is exactly
    # one warble cycle, with a short crossfade at the seam.
    def b_ufo():
        ufo = resample(p('ufo.wav'), 5495, f'{TMP}/ufo.wav')
        N = 16
        for i in range(N):
            k = len(ufo) - N + i
            ufo[k] = (ufo[k] * (N - i) + ufo[i] * i) // N
        return ufo, False
    add('ufo', b_ufo)

    # The destruction sound is a ~2 s warble that rings out once:
    # 2747 Hz ADPCM, cut at 2.05 s with a short fade.
    def b_ufohit():
        ufohit = trim(resample(p('ufohit.wav'), 2747,
                               f'{TMP}/ufohit.wav'))
        if len(ufohit) > 5632:
            ufohit = ufohit[:5632]
        return gain(fade_tail(ufohit, 96), 1.2), True
    add('ufohit', b_ufohit)

    if len(missing) == len(ENGINE_LENGTHS):
        print("sounds/ not found -- generating SILENT sound data "
              "(the game will play without sound)")
    elif missing:
        print("no WAV for: " + ", ".join(missing) +
              " -- those sounds will be silent")
    return plan


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    os.makedirs(TMP, exist_ok=True)
    plan = plan_from_sounds()

    total = 0
    with open(os.path.join(OUTDIR, 'sound_data.asm'), 'w') as f:
        f.write('; GENERATED by tools/gen_sound_data.py -- '
                'do not edit by hand.\n')
        f.write('; Sample data (in RODATA, loaded with the main file).\n')
        f.write('; ADPCM blobs are IMA 4-bit (low nibble first); raw '
                'blobs are\n')
        f.write('; unsigned 8-bit, 128 = center.\n')
        f.write('        .segment "RODATA"\n')
        for name, data, enc in plan:
            blob = adpcm_encode(data) if enc else data
            total += len(blob)
            f.write('        .export _snd_data_%s\n_snd_data_%s:\n'
                    % (name, name))
            for i in range(0, len(blob), 16):
                f.write('        .byte %s\n'
                        % ','.join('$%02X' % b for b in blob[i:i + 16]))
            note = ''
            if enc:
                note = ' adpcm, SNR %.1f dB' % snr(
                    data, adpcm_decode(blob, len(data)))
            print('%-10s %5d samples -> %5d bytes%s'
                  % (name, len(data), len(blob), note))
    print('TOTAL %d bytes' % total)

    # IMA-ADPCM vpdiff table: pure math, no sample data involved.
    with open(os.path.join(OUTDIR, 'vpdiff_table.asm'), 'w') as f:
        f.write('; GENERATED by tools/gen_sound_data.py -- '
                'do not edit by hand.\n')
        f.write('; IMA-ADPCM vpdiff table: index = step_index*8 + '
                '(nibble & 7).\n')
        f.write('        .segment "RODATA"\n')
        f.write('        .export _snd_vpdiff_tab\n_snd_vpdiff_tab:\n')
        words = []
        for si, step in enumerate(STEP):
            for nib in range(8):
                vp = (step >> 3) + (step if nib & 4 else 0) + \
                     ((step >> 1) if nib & 2 else 0) + \
                     ((step >> 2) if nib & 1 else 0)
                words.append(vp)
        for i in range(0, len(words), 8):
            f.write('        .word %s\n'
                    % ','.join(str(w) for w in words[i:i + 8]))
    print('vpdiff table: %d words (%d bytes)' % (len(words), 2 * len(words)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
