#!/usr/bin/env python3
"""Generate the original Phase / Space score: 180 seconds, 120 BPM, stereo PCM.

Requires numpy only. No samples, external instruments or project mutations.
A float32 stereo mix and mono ambience bus bound working audio storage to
approximately 105 MB; synthesis and encoding use short event/block buffers.
"""
import argparse
import json
import math
from pathlib import Path
import wave

import numpy as np

RATE = 48000
SECONDS = 180
COUNT = RATE * SECONDS
BEAT = .5
SEED = 20260926
SECTIONS = [(0, 16, "Arrival"), (16, 48, "Orbit"), (48, 80, "Structure"),
            (80, 112, "Drift"), (112, 144, "Convergence"),
            (144, 168, "Departure"), (168, 180, "Resolution")]


def hz(note):
    return 440 * 2 ** ((note - 69) / 12)


def clock(duration):
    return np.arange(round(duration * RATE), dtype=np.float32) / RATE


def envelope(t, attack, release):
    return np.minimum(t / attack, 1) * np.minimum((t.size / RATE - t) / release, 1)


def write_wave(path, mix):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(RATE)
        for first in range(0, len(mix), RATE):
            block = np.rint(np.clip(mix[first:first + RATE], -1, 1) * 32767).astype("<i2")
            output.writeframesraw(block.tobytes())


def render():
    rng = np.random.default_rng(SEED)
    mix = np.zeros((COUNT, 2), dtype=np.float32)
    ambience = np.zeros(COUNT, dtype=np.float32)

    def place(sound, when, level=1, pan=0, send=0):
        first = round(when * RATE)
        skip = max(0, -first)
        first = max(0, first)
        size = min(len(sound) - skip, COUNT - first)
        if size <= 0:
            return
        sound = sound[skip:skip + size]
        angle = (pan + 1) * math.pi / 4
        mix[first:first + size, 0] += sound * (level * math.cos(angle))
        mix[first:first + size, 1] += sound * (level * math.sin(angle))
        if send:
            ambience[first:first + size] += sound * (level * send)

    def pluck(note, when, level=.12, pan=0, duration=1.5, glass=False):
        t = clock(duration)
        f = hz(note)
        phase = 2 * np.pi * f * t
        # Decaying phase modulation produces a bell attack, resolving to a tone.
        modulation = (1.8 if glass else .8) * np.exp(-t * 6) * np.sin(phase * (2.001 if glass else 2))
        sound = np.sin(phase + modulation) * np.exp(-t * (3.5 if glass else 5))
        sound *= envelope(t, .006, .08)
        place(sound, when, level, pan, .55)
        place(sound, when + .375, level * .19, -pan, .1)
        place(sound, when + .75, level * .075, pan, 0)

    def bass(note, when, duration=.32, level=.22):
        t = clock(duration)
        phase = 2 * np.pi * hz(note) * t
        sound = (np.sin(phase) + .19 * np.sin(2 * phase) + .07 * np.sin(3 * phase))
        sound *= np.exp(-t * 2) * envelope(t, .009, .065)
        place(sound, when, level)

    def pad(notes, when, duration, level=.055):
        t = clock(duration)
        # Two independently detuned voices per pitch; slow amplitude motion.
        for index, note in enumerate(notes):
            f = hz(note)
            phase = 2 * np.pi * f * t
            sound = np.sin(phase * .9991 + index) + .65 * np.sin(phase * 1.0013 + index * .6)
            sound += .12 * np.sin(phase * 2) + .035 * np.sin(phase * 3)
            sound *= envelope(t, min(1.2, duration / 4), min(2, duration / 3))
            sound *= .8 + .2 * np.sin(2 * np.pi * .13 * t + index)
            # A gentle quarter-note breathing pulse, kept subtle in the pad.
            sound *= 1 - .18 * np.exp(-np.remainder(t + when, BEAT) * 12)
            place(sound, when, level / math.sqrt(len(notes)), -.7 + 1.4 * index / max(1, len(notes) - 1), .35)

    def kick(when, level=.5):
        t = clock(.48)
        phase = 2 * np.pi * (43 * t + 92 * .019 * (1 - np.exp(-t / .019)))
        sound = np.sin(phase) * np.exp(-t * 10)
        sound += rng.uniform(-1, 1, t.size).astype(np.float32) * np.exp(-t * 650) * .12
        sound *= envelope(t, .001, .045)
        place(sound, when, level)

    def snare(when, level=.17):
        t = clock(.24)
        noise = rng.standard_normal(t.size).astype(np.float32)
        high = noise - np.roll(noise, 1)
        sound = high * .24 * np.exp(-t * 24) + np.sin(2 * np.pi * 185 * t) * .3 * np.exp(-t * 35)
        sound *= envelope(t, .0015, .03)
        place(sound, when, level, .1, .12)

    def hat(when, level=.04, pan=0, opened=False):
        t = clock(.23 if opened else .065)
        noise = rng.standard_normal(t.size).astype(np.float32)
        sound = (noise - np.roll(noise, 1)) * np.exp(-t * (28 if opened else 90))
        sound *= envelope(t, .001, .015)
        place(sound, when, level, pan)

    def swell(when, duration=2, level=.09):
        t = clock(duration)
        noise = rng.standard_normal(t.size).astype(np.float32)
        # Smoothed noise instead of a harsh full-band riser.
        noise = np.convolve(noise, np.ones(19, dtype=np.float32) / 19, mode="same")
        sound = noise * (t / duration) ** 2 * envelope(t, .2, .04)
        place(sound, when, level, -.25, .45)

    # D minor/add9, Bb major/add9, F major/add9, suspended C. The original
    # five-note motif returns throughout, rather than an endless random arpeggio.
    chords = [(50, 57, 60, 64), (46, 53, 57, 60), (41, 53, 57, 60), (48, 55, 58, 62)]
    roots = [38, 34, 29, 36]
    motif = [(0, 74), (1.5, 69), (3, 72), (4.5, 77), (6.5, 76)]
    for first, last, name in SECTIONS:
        for when in range(first, last, 8):
            chord = chords[((when - 16) // 8) % 4] if name != "Resolution" else chords[0]
            level = .044 if name in ("Arrival", "Drift", "Resolution") else .065
            pad(chord, when, min(10, last - when + .8), level)

    for bar in range(90):
        when = bar * 2
        if when < 16 or 80 <= when < 112 or when >= 168:
            continue
        climax = 112 <= when < 144
        structure = 48 <= when < 80
        departure = when >= 144
        density = max(.25, 1 - (when - 144) / 30) if departure else 1
        # Four-beat groove with displaced pickup kicks and phrase-ending rests.
        kicks = [0, 2] if not climax else [0, 1.5, 2, 3]
        if structure:
            kicks = [0, 1.75, 2.5] if bar % 2 else [0, 2.5]
        if bar % 8 == 7:
            kicks = [0, 2]
        for beat in kicks:
            kick(when + beat * BEAT, (.47 if climax else .39) * density)
        for beat in [1, 3]:
            if not departure or bar % 2 == 0:
                snare(when + beat * BEAT + .008, .2 * density if climax else .15 * density)
        for step in range(8):
            if (bar + step) % 5 == 0 or (departure and step % 2 == 0):
                continue
            swing = .018 if step % 2 else 0
            hat(when + step * .25 + swing, (.028 + .017 * (step % 2)) * density,
                -.45 if step % 2 else .45, opened=step == 7 and bar % 4 == 3)
        root = roots[((when - 16) // 8) % 4]
        for beat, transpose in [(0, 0), (1.5, 0), (2.75, 12)]:
            if departure and beat != 0 and bar % 2:
                continue
            bass(root + transpose, when + beat * BEAT, .42 if beat == 0 else .24, .24 * density)
        if structure or climax:
            chord = chords[((when - 16) // 8) % 4]
            for step in range(8):
                if step in (3, 7) and bar % 2:
                    continue
                pitch = chord[(step + bar) % 4] + 24
                pluck(pitch, when + step * .25, .058 if structure else .075,
                      -.65 + 1.3 * step / 7, .85)

    for phrase in range(0, 176, 8):
        if 80 <= phrase < 88:  # Deliberate low-density hold at the corridor exit.
            continue
        sparse = phrase < 16 or 88 <= phrase < 112 or phrase >= 160
        for index, (beat, note) in enumerate(motif):
            if sparse and index not in (0, 3):
                continue
            if phrase >= 168 and index != 0:
                continue
            transpose = -12 if 88 <= phrase < 112 else 0
            pluck(note + transpose, phrase + beat * BEAT, .14 if 112 <= phrase < 144 else .11,
                  -.35 if index % 2 else .35, 2.8 if sparse else 1.8, True)
    for cue in [16, 48, 112, 144, 168]:
        swell(cue - 2, 2, .14 if cue == 112 else .08)
        if cue in (48, 112):
            pluck(38, cue, .26, 0, 3, True)
    # Final suspended ninth resolves gently, leaving a full second of silence.
    pluck(74, 168, .12, -.2, 4, True)
    pluck(69, 172, .09, .25, 4, True)
    pluck(62, 175, .08, 0, 4, True)

    # Fixed multi-tap ambience, applied in blocks from an immutable mono bus.
    # Prime-ish delay lengths decorrelate stereo without moving the low end.
    for delay, gain, pan in [(.071, .15, -.8), (.113, .13, .8), (.227, .11, -.6),
                             (.373, .095, .65), (.557, .07, -.4), (.811, .055, .5),
                             (1.173, .035, -.7), (1.619, .025, .7)]:
        offset = round(delay * RATE)
        angle = (pan + 1) * math.pi / 4
        for first in range(offset, COUNT, RATE):
            end = min(COUNT, first + RATE)
            source = ambience[first - offset:end - offset]
            mix[first:end, 0] += source * (gain * math.cos(angle))
            mix[first:end, 1] += source * (gain * math.sin(angle))
    del ambience
    # Quiet half-bar breath immediately before the two strongest changes.
    for cue in (48, 112):
        first = round((cue - .45) * RATE)
        fade = np.linspace(1, .13, round(.43 * RATE), dtype=np.float32)
        mix[first:first + len(fade)] *= fade[:, None]
        mix[round((cue - .02) * RATE):round(cue * RATE)] *= .13
        recovery = np.linspace(.13, 1, round(.005 * RATE), dtype=np.float32)
        mix[round(cue * RATE):round(cue * RATE) + len(recovery)] *= recovery[:, None]
    # Sudden thinning at80s includes effect tails, not only new note onsets.
    hold = round(80 * RATE)
    fade = np.linspace(1, .24, round(.04 * RATE), dtype=np.float32)
    mix[hold:hold + len(fade)] *= fade[:, None]
    mix[hold + len(fade):82 * RATE] *= .24
    mix[82 * RATE:84 * RATE] *= np.linspace(.24, 1, 2 * RATE, dtype=np.float32)[:, None]
    for first in range(0, COUNT, RATE):
        block = mix[first:first + RATE]
        np.tanh(block * 1.15, out=block)
    peak = float(np.max(np.abs(mix)))
    mix *= .92 / max(peak, 1e-12)
    mix[:RATE] *= np.linspace(0, 1, RATE, dtype=np.float32)[:, None]
    mix[176 * RATE:179 * RATE] *= np.linspace(1, 0, 3 * RATE, dtype=np.float32)[:, None]
    mix[179 * RATE:] = 0
    return mix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Destination stereo 48 kHz / 16-bit WAV")
    parser.add_argument("--excerpts-dir", type=Path, help="Also write short review excerpts around the major cues")
    args = parser.parse_args()
    mix = render()
    if not np.isfinite(mix).all() or mix.shape != (COUNT, 2):
        raise RuntimeError("Invalid generated audio")
    write_wave(args.output, mix)
    stats = []
    for first, last, name in SECTIONS:
        section = mix[first * RATE:last * RATE]
        energy = sum(float(np.sum(np.square(section[i:i + RATE], dtype=np.float64))) for i in range(0, len(section), RATE))
        stats.append({"name": name, "start": first, "end": last,
                      "rms_dbfs": round(20 * math.log10(max(math.sqrt(energy / section.size), 1e-12)), 2)})
    if args.excerpts_dir:
        for first, last, name in [(44, 56, "corridor"), (76, 88, "hold"), (108, 120, "convergence"), (168, 180, "resolution")]:
            write_wave(args.excerpts_dir / (name + ".wav"), mix[first * RATE:last * RATE])
    with wave.open(str(args.output), "rb") as check:
        assert (check.getnframes(), check.getframerate(), check.getnchannels(), check.getsampwidth()) == (COUNT, RATE, 2, 2)
    print(json.dumps({"output": str(args.output), "seconds": SECONDS, "frames": COUNT,
                      "sample_rate": RATE, "channels": 2, "bits": 16,
                      "peak": float(np.max(np.abs(mix))), "endpoint_samples": [mix[0].tolist(), mix[-1].tolist()],
                      "sections": stats}, indent=2))


if __name__ == "__main__":
    main()
