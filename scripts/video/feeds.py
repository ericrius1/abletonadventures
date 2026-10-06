#!/usr/bin/env python3
"""Renders the shared audio feeds for the showcase video using the pack's own instruments.

  feeds.py [material_dir] [feeds_dir]

Each feed is 12 s (6 bars of Am-F-C-G-Am-F at 120 BPM), 48 kHz stereo:
  drums.wav  Critter Kit playing drums.mid (built-in sequencer off)
  keys.wav   Stardust "Galactic Pluck" playing arp8.mid
  pad.wav    Stardust "Stardust Pad" playing chords.mid
  bass.wav   Stardust "Cosmic Bass" playing bass.mid
  vocal.wav  Babble "Hello Choir" singing melody_low.mid
  groove.wav drums + bass + keys        (input for the effects)
  band.wav   drums + bass + pad + keys  (title/outro music)
"""
import subprocess, sys, wave
from pathlib import Path
import numpy as np

HARNESS = "/home/user/build/tools/harness/aa_harness_artefacts/Release/AdventureHarness"
SR = 48000
SECONDS = 12


def vst3(plugin):
    return str(sorted(Path(f"/home/user/build-{plugin}/plugins/{plugin}/{plugin}_artefacts/Release/VST3").glob("*.vst3"))[0])


def read(path):
    with wave.open(str(path)) as w:
        raw = w.readframes(w.getnframes())
        ch = w.getnchannels()
    a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
    s = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
    s = np.where(s >= 2 ** 23, s - 2 ** 24, s) / 2 ** 23
    return s.reshape(-1, ch)


def write(path, a):
    pcm = (np.clip(a, -1, 1) * (2 ** 23 - 1)).astype(np.int32)
    b = np.zeros((pcm.size, 3), dtype=np.uint8)
    flat = pcm.reshape(-1)
    b[:, 0] = flat & 0xFF
    b[:, 1] = (flat >> 8) & 0xFF
    b[:, 2] = (flat >> 16) & 0xFF
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(3)
        w.setframerate(SR)
        w.writeframes(b.tobytes())


def rms_db(a):
    return 20 * np.log10(np.sqrt(np.mean(a.mean(axis=1) ** 2)) + 1e-9)


def render(plugin, midi, out, preset=None, params=()):
    cmd = [HARNESS, "render", vst3(plugin), str(out), "--seconds", str(SECONDS), "--midi-file", str(midi), "--input", "silence"]
    if preset:
        cmd += ["--preset", preset]
    for p in params:
        cmd += ["--param", p]
    r = subprocess.run(cmd, capture_output=True, text=True)
    line = [l for l in r.stdout.splitlines() if l.startswith("RESULT")]
    print(f"  {out.name:10s} {line[0] if line else r.stdout[-400:]}")


def normalise(a, target_db=-18.0):
    return a * 10 ** ((target_db - rms_db(a)) / 20)


def main():
    material = Path(sys.argv[1] if len(sys.argv) > 1 else "/home/user/video/material")
    feeds = Path(sys.argv[2] if len(sys.argv) > 2 else "/home/user/video/feeds")
    feeds.mkdir(parents=True, exist_ok=True)

    render("CritterKit", material / "drums.mid", feeds / "drums.wav", params=["Sequencer=Off"])
    render("Stardust", material / "arp8.mid", feeds / "keys.wav", preset="Galactic Pluck")
    render("Stardust", material / "chords.mid", feeds / "pad.wav", preset="Stardust Pad")
    render("Stardust", material / "bass.mid", feeds / "bass.wav", preset="Cosmic Bass")
    render("Babble", material / "melody_low.mid", feeds / "vocal.wav", preset="Hello Choir")

    d, b, k, p = (normalise(read(feeds / f"{n}.wav")) for n in ("drums", "bass", "keys", "pad"))
    n = min(len(d), len(b), len(k), len(p))
    for name, mix in (("groove", d[:n] + 0.75 * b[:n] + 0.55 * k[:n]),
                      ("band", d[:n] + 0.7 * b[:n] + 0.45 * p[:n] + 0.5 * k[:n])):
        mix = mix * (10 ** (-1.0 / 20) / (np.max(np.abs(mix)) + 1e-9))
        write(feeds / f"{name}.wav", mix)
        print(f"  {name + '.wav':10s} mixed, rms {rms_db(mix):.1f} dBFS")


if __name__ == "__main__":
    main()
