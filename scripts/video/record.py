#!/usr/bin/env python3
"""Records one showcase segment from a JSON spec using the test harness (Linux, Xvfb).

  record.py spec.json [--out /home/user/video/rec]

Spec fields:
  plugin        plugin folder name (e.g. "Boing")
  preset        factory preset name to load first        (optional)
  params        ["Param Name=value text", ...]          (optional)
  automations   ["Param Name=from:to@t0:t1", ...]       (optional, seconds from audio start)
  midiFile      path to a .mid file                      (optional)
  inputFile     path to a .wav file fed as input         (optional)
  builtinInput  harness --input name (drums|pluck|...)   (optional)
  builtinMidi   harness --midi name (none|chords|...)    (optional)
  width         editor width in pixels (default 1320)
  recordSeconds audio length to record (default 9)
  clipStart     where the 6 s clip starts inside the recording (default 2.0)
  clipDuration  clip length (default 6.0)

Outputs <out>/<plugin>.{mkv,wav,json}, <plugin>.sync.json (sync offset), <plugin>_sheet.png (6 frames
from the clip window) and prints per-second RMS/peak of the clip.
"""
import json, subprocess, sys, os, zlib, wave, struct
from pathlib import Path
import numpy as np

HARNESS = "/home/user/build/tools/harness/aa_harness_artefacts/Release/AdventureHarness"


def display_for(name):
    # same scheme as scripts/dev.sh: cksum-based number
    out = subprocess.run(["bash", "-c", f"printf '%s' '{name}' | cksum | cut -d' ' -f1"], capture_output=True, text=True).stdout
    return f":{int(out) % 400 + 100}"


def ensure_xvfb(display):
    if subprocess.run(["pgrep", "-f", f"Xvfb {display} -screen"], capture_output=True).returncode != 0:
        subprocess.Popen(["Xvfb", display, "-screen", "0", "2600x1700x24", "-nolisten", "tcp"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        import time; time.sleep(1.0)


def find_vst3(plugin):
    base = Path(f"/home/user/build-{plugin}/plugins/{plugin}/{plugin}_artefacts/Release/VST3")
    found = sorted(base.glob("*.vst3"))
    if not found:
        sys.exit(f"no VST3 for {plugin} in {base} - build it with scripts/dev.sh {plugin} build")
    return str(found[0])


def read_wav(path):
    with wave.open(str(path)) as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
    s = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
    s = np.where(s >= 2 ** 23, s - 2 ** 24, s) / 2 ** 23
    return s.reshape(-1, ch), sr


def sync_offset(mkv, meta):
    w, h, strip = meta["width"], meta["height"], meta["stripHeight"]
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(mkv), "-vf",
                          f"crop={w}:{strip}:0:{h - strip},scale=1:1,format=gray", "-f", "rawvideo", "-"],
                         capture_output=True).stdout
    for i, v in enumerate(raw):
        if v > 128:
            return i / 30.0
    sys.exit("sync strip never turned white - recording failed?")


def main():
    spec_path = Path(sys.argv[1])
    out_dir = Path(sys.argv[sys.argv.index("--out") + 1]) if "--out" in sys.argv else Path("/home/user/video/rec")
    out_dir.mkdir(parents=True, exist_ok=True)
    spec = json.loads(spec_path.read_text())
    plugin = spec["plugin"]
    prefix = out_dir / plugin
    seconds = float(spec.get("recordSeconds", 9))
    clip_start = float(spec.get("clipStart", 2.0))
    clip_dur = float(spec.get("clipDuration", 6.0))

    cmd = [HARNESS, "record", find_vst3(plugin), str(prefix), "--seconds", str(seconds),
           "--width", str(spec.get("width", 1320)), "--lead-in", "1.5"]
    if spec.get("preset"):
        cmd += ["--preset", spec["preset"]]
    for p in spec.get("params", []):
        cmd += ["--param", p]
    for a in spec.get("automations", []):
        cmd += ["--automate", a]
    if spec.get("midiFile"):
        cmd += ["--midi-file", spec["midiFile"]]
    if spec.get("inputFile"):
        cmd += ["--input-file", spec["inputFile"]]
    cmd += ["--input", spec.get("builtinInput", "auto"), "--midi", spec.get("builtinMidi", "auto")]

    display = spec.get("display") or display_for(plugin)
    ensure_xvfb(display)
    env = dict(os.environ, DISPLAY=display)
    res = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=seconds + 90)
    print("\n".join(l for l in res.stdout.splitlines() if l.strip()))
    if "RECORDED" not in res.stdout:
        print(res.stderr[-2000:])
        sys.exit("recording failed")

    meta = json.loads(prefix.with_suffix(".json").read_text())
    offset = sync_offset(prefix.with_suffix(".mkv"), meta)
    prefix.with_suffix(".sync.json").write_text(json.dumps({"offset": offset, **meta}))
    print(f"sync offset {offset:.3f}s, capture {meta['width']}x{meta['editorHeight']}")

    # contact sheet: 6 frames across the clip window (editor area only)
    times = [offset + clip_start + clip_dur * (i + 0.5) / 6 for i in range(6)]
    tiles = []
    for i, t in enumerate(times):
        f = out_dir / f".{plugin}_f{i}.png"
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", f"{t:.3f}", "-i", str(prefix.with_suffix(".mkv")),
                        "-frames:v", "1", "-vf", f"crop={meta['width']}:{meta['editorHeight']}:0:0,scale=640:-2", str(f)],
                       check=True)
        tiles.append(str(f))
    subprocess.run(["montage", *tiles, "-tile", "3x2", "-geometry", "+4+4", "-background", "#222",
                    str(out_dir / f"{plugin}_sheet.png")], check=True)
    for f in tiles:
        os.remove(f)
    print("sheet", out_dir / f"{plugin}_sheet.png")

    # smoothness: fraction of frames in the clip identical to the previous one (UI didn't repaint in time)
    raw = subprocess.run(["ffmpeg", "-v", "error", "-ss", f"{offset + clip_start:.3f}", "-t", f"{clip_dur:.3f}",
                          "-i", str(prefix.with_suffix(".mkv")), "-vf",
                          f"crop={meta['width']}:{meta['editorHeight']}:0:0,scale=320:-2,format=gray",
                          "-f", "rawvideo", "-"], capture_output=True).stdout
    fw = 320
    fh = int(round(meta["editorHeight"] * 320 / meta["width"] / 2) * 2)
    frames = np.frombuffer(raw, dtype=np.uint8)
    nfr = len(frames) // (fw * fh)
    if nfr > 1:
        frames = frames[: nfr * fw * fh].reshape(nfr, fh, fw).astype(np.int16)
        diffs = np.abs(np.diff(frames, axis=0)).mean(axis=(1, 2))
        dup = float(np.mean(diffs < 0.05))
        print(f"frames {nfr}, duplicated (static) frames {dup * 100:.0f}% (high values mean stutter or a static UI)")

    audio, sr = read_wav(prefix.with_suffix(".wav"))
    clip = audio[int(clip_start * sr): int((clip_start + clip_dur) * sr)]
    mono = clip.mean(axis=1)
    stats = []
    for s in range(int(clip_dur)):
        seg = mono[s * sr:(s + 1) * sr]
        rms = 20 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-9)
        pk = 20 * np.log10(np.max(np.abs(clip[s * sr:(s + 1) * sr])) + 1e-9)
        stats.append(f"{rms:.0f}/{pk:.0f}")
    print("clip rms/peak dB per second:", " ".join(stats))
    total_rms = 20 * np.log10(np.sqrt(np.mean(mono ** 2)) + 1e-9)
    print(f"clip total rms {total_rms:.1f} dBFS, peak {20*np.log10(np.max(np.abs(clip))+1e-9):.1f} dBFS, "
          f"nan {int(np.isnan(clip).sum())}")


if __name__ == "__main__":
    main()
