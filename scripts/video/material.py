#!/usr/bin/env python3
"""Generates the shared musical material for the showcase video (MIDI files).

Everything is in A minor at 120 BPM (one bar = 2 s), progression Am - F - C - G - Am - F,
so any 3-bar window is exactly 6 seconds and every segment sounds like the same song.
"""
import sys
from pathlib import Path
import mido

BPM = 120
TPB = 480                      # ticks per beat
BAR = TPB * 4
BARS = 6

# chord voicings (MIDI notes), one per bar
CHORDS = [
    [45, 57, 60, 64],   # Am
    [41, 57, 60, 65],   # F
    [48, 55, 60, 64],   # C
    [43, 55, 59, 62],   # G
    [45, 57, 60, 64],   # Am
    [41, 57, 60, 65],   # F
]
ROOTS = [45, 41, 48, 43, 45, 41]
ARP_SHAPES = [
    [57, 60, 64, 69, 72, 69, 64, 60],
    [53, 57, 60, 65, 69, 65, 60, 57],
    [55, 60, 64, 67, 72, 67, 64, 60],
    [55, 59, 62, 67, 71, 67, 62, 59],
    [57, 60, 64, 69, 72, 69, 64, 60],
    [53, 57, 60, 65, 69, 65, 60, 57],
]
# (beat position in quarter notes, length in quarters, note) - a singable A-minor line
MELODY = [
    (0, 1, 69), (1, 0.5, 67), (1.5, 0.5, 64), (2, 2, 64),
    (4, 1, 65), (5, 0.5, 64), (5.5, 0.5, 60), (6, 2, 60),
    (8, 1, 64), (9, 1, 67), (10, 1, 72), (11, 0.5, 71), (11.5, 0.5, 67),
    (12, 3, 71), (15, 0.5, 69), (15.5, 0.5, 67),
    (16, 1.5, 69), (17.5, 0.5, 64), (18, 1, 69), (19, 1, 72),
    (20, 4, 69),
]
# Critter Kit pads: 36 kick, 37 snare, 38 clap, 39 closed hat, 40 open hat, 41 tom, 42 clink, 43 zap
DRUMS = {
    36: "x.....x.x.x.....",
    37: "....x.......x...",
    38: "....x.......x..x",
    39: "x.xxx.x.x.xxx.x.",
    40: "......x.......x.",
    42: "...x..x...x...x.",
}
FILL_TOM = "........x.x.xxxx"   # last bar of each 2-bar phrase... used on bars 4 and 6
ZAP_BARS = {1, 3, 5}           # a zap on the last 16th of these bars


def new_file():
    f = mido.MidiFile(ticks_per_beat=TPB)
    t = mido.MidiTrack()
    f.tracks.append(t)
    t.append(mido.MetaMessage("set_tempo", tempo=mido.bpm2tempo(BPM), time=0))
    t.append(mido.MetaMessage("time_signature", numerator=4, denominator=4, time=0))
    return f, t


def write(events, path, channel=0):
    """events: list of (start_tick, length_ticks, note, velocity)"""
    f, track = new_file()
    msgs = []
    for start, length, note, vel in events:
        msgs.append((int(start), 1, mido.Message("note_on", note=note, velocity=vel, channel=channel)))
        msgs.append((int(start + length), 0, mido.Message("note_off", note=note, velocity=0, channel=channel)))
    msgs.sort(key=lambda m: (m[0], m[1]))
    now = 0
    for tick, _, msg in msgs:
        track.append(msg.copy(time=tick - now))
        now = tick
    track.append(mido.MetaMessage("end_of_track", time=TPB))
    f.save(path)


def main(out_dir):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    write([(b * BAR, BAR - 10, n, 92) for b in range(BARS) for n in CHORDS[b]], out / "chords.mid")

    mel = [(int(p * TPB), int(l * TPB) - 20, n, 100) for p, l, n in MELODY]
    write(mel, out / "melody.mid")
    write([(s, l, n - 12, v) for s, l, n, v in mel], out / "melody_low.mid")
    write(mel + [(b * BAR, BAR - 10, n, 70) for b in range(BARS) for n in CHORDS[b]], out / "chords_melody.mid")

    arp = []
    for b in range(BARS):
        for i in range(16):
            n = ARP_SHAPES[b][i % 8] + (12 if i >= 8 and b % 2 == 1 else 0)
            arp.append((b * BAR + i * TPB // 4, TPB // 4 - 20, n, 112 if i % 4 == 0 else 84))
    write(arp, out / "arp.mid")

    eighth_arp = [(s, l * 2, n, v) for s, l, n, v in arp if (s // (TPB // 4)) % 2 == 0]
    write(eighth_arp, out / "arp8.mid")

    bass = []
    for b in range(BARS):
        for i, (pos, length, octave) in enumerate([(0, 1.5, 0), (1.5, 0.5, 12), (2, 1, 0), (3, 0.5, 0), (3.5, 0.5, 12)]):
            bass.append((b * BAR + int(pos * TPB), int(length * TPB) - 20, ROOTS[b] - 12 + octave, 110 if i == 0 else 90))
    write(bass, out / "bass.mid")

    write([(0, BAR * BARS - 10, n, 96) for n in [45, 57, 60, 64]], out / "hold_am.mid")
    write([(b * BAR + beat * TPB * 2, TPB * 2 - 30, CHORDS[b][1 + (beat % 3)] + 12, 100)
           for b in range(BARS) for beat in range(2)], out / "sparse.mid")

    drums = []
    for b in range(BARS):
        for note, pattern in DRUMS.items():
            for i, c in enumerate(pattern):
                if c == "x":
                    vel = 118 if i % 4 == 0 else 92
                    drums.append((b * BAR + i * TPB // 4, TPB // 8, note, vel))
        if b in (3, 5):
            for i, c in enumerate(FILL_TOM):
                if c == "x":
                    drums.append((b * BAR + i * TPB // 4, TPB // 8, 41, 100))
        if b in ZAP_BARS:
            drums.append((b * BAR + 15 * TPB // 4, TPB // 8, 43, 110))
    write(drums, out / "drums.mid", channel=9)

    print("wrote", sorted(p.name for p in out.glob("*.mid")))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "/home/user/video/material")
