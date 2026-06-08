#!/usr/bin/env python3
"""Generate PLACEHOLDER sound effects for the bundled SFX library so the
pipeline (manifest -> library -> picker -> preview -> gameplay) can be tested
before the real CC0 sounds are chosen. Pure stdlib (wave + struct + math).

These are intentionally simple synthesized blips/tones. Replace them with the
approved CC0 library and update sfx/manifest.json accordingly.
"""
import math
import os
import struct
import wave

RATE = 44100
HERE = os.path.dirname(os.path.abspath(__file__))
SFX = os.path.join(HERE, "..", "sfx")


def write_wav(rel, samples):
    path = os.path.join(SFX, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples)
        w.writeframes(frames)
    print("wrote", rel, "(%d frames)" % len(samples))


def tone(freq, ms, decay=True, amp=0.6, square=False):
    n = int(RATE * ms / 1000)
    out = []
    for i in range(n):
        t = i / RATE
        env = (1.0 - i / n) if decay else 1.0
        if decay:
            env *= env
        v = math.sin(2 * math.pi * freq * t)
        if square:
            v = 1.0 if v >= 0 else -1.0
        out.append(v * env * amp)
    return out


def sweep(f0, f1, ms, amp=0.6):
    n = int(RATE * ms / 1000)
    out = []
    for i in range(n):
        t = i / n
        f = f0 + (f1 - f0) * t
        env = math.sin(math.pi * t)  # fade in/out
        out.append(math.sin(2 * math.pi * f * (i / RATE)) * env * amp)
    return out


def noise_swipe(ms, amp=0.5):
    import random
    random.seed(1)
    n = int(RATE * ms / 1000)
    out = []
    prev = 0.0
    for i in range(n):
        t = i / n
        env = math.sin(math.pi * t)
        white = random.uniform(-1, 1)
        prev = prev * 0.7 + white * 0.3  # low-pass for a softer whoosh
        out.append(prev * env * amp)
    return out


def loop_tone(freqs, cycles_of, ms, amp=0.35):
    # Seamless loop: length is an integer number of cycles of the base tone.
    base = freqs[0]
    period = RATE / base
    n = int(round(period * cycles_of))
    out = []
    for i in range(n):
        t = i / RATE
        v = sum(math.sin(2 * math.pi * f * t) for f in freqs) / len(freqs)
        out.append(v * amp)
    return out


def main():
    # short/
    write_wav("short/click.wav",   tone(1200, 30))
    write_wav("short/tap.wav",     tone(800, 40))
    write_wav("short/blip.wav",    tone(1600, 18))
    write_wav("short/confirm.wav", tone(600, 70) + tone(900, 90))
    write_wav("short/back.wav",    sweep(520, 300, 120))
    write_wav("short/toggle.wav",  tone(1000, 45, square=True, amp=0.4))
    write_wav("short/scroll.wav",  tone(2000, 14, amp=0.4))
    write_wav("short/swipe.wav",   noise_swipe(120))
    # long/ (seamless loops)
    write_wav("long/drone.wav",    loop_tone([220, 220.5], 220, 1000))
    write_wav("long/pad.wav",      loop_tone([330, 440], 330, 1000))


if __name__ == "__main__":
    main()
