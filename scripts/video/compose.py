#!/usr/bin/env python3
"""Composes the showcase video from recorded segments (see record.py).

  compose.py cut.json out.mp4 [--rec /home/user/video/rec] [--work /home/user/video/work]

cut.json:
{
  "intro":  {"seconds": 4, "audio": "feeds/intro.wav", "audioStart": 0},
  "outro":  {"seconds": 4, "audio": "feeds/outro.wav", "audioStart": 0},
  "transition": 0.35,
  "segments": [
    {"plugin": "Boing", "title": "Boing", "tagline": "a bouncing-ball delay", "kind": "effect",
     "font": "LuckiestGuy-Regular.ttf", "accent": "#ff5a5f", "colors": ["#bfe6ff", "#d9ccff"],
     "clipStart": 2.0, "clipDuration": 6.0}
  ]
}
Fonts are looked up in assets/fonts. Thumbnails for the title cards come from docs/screenshots.
"""
import json, subprocess, sys, wave
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "assets" / "fonts"
W, H, FPS = 1920, 1080, 30
SR = 48000
PLUGIN_H = 860           # plugin height on screen
PLUGIN_TOP = 44
CAPTION_Y = 948


def font(name, size):
    return ImageFont.truetype(str(FONTS / name), size)


def hex_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(" ".join(map(str, cmd)))
        print(r.stderr[-3000:])
        sys.exit("ffmpeg failed")
    return r


# ---------------------------------------------------------------------------------------------
# audio helpers
def read_wav(path):
    with wave.open(str(path)) as w:
        n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(n)
    if sw == 3:
        a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        s = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
        s = np.where(s >= 2 ** 23, s - 2 ** 24, s) / 2 ** 23
    else:
        s = np.frombuffer(raw, dtype=np.int16) / 32768.0
    return s.reshape(-1, ch)


def write_wav(path, audio):
    a = np.clip(audio, -1, 1)
    pcm = (a * 32767).astype(np.int16)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def prepare_audio(src, start, seconds, out, target_rms_db=-17.0, ceiling_db=-1.5, fade_in=0.01, fade_out=0.01):
    audio = read_wav(src)
    if audio.shape[1] == 1:
        audio = np.repeat(audio, 2, axis=1)
    a = audio[int(start * SR): int((start + seconds) * SR)]
    if len(a) < int(seconds * SR):
        a = np.vstack([a, np.zeros((int(seconds * SR) - len(a), 2))])
    rms = np.sqrt(np.mean(a.mean(axis=1) ** 2)) + 1e-9
    gain = 10 ** (target_rms_db / 20) / rms
    peak = np.max(np.abs(a)) + 1e-9
    gain = min(gain, 10 ** (ceiling_db / 20) / peak)
    a = a * gain
    n_in, n_out = max(1, int(fade_in * SR)), max(1, int(fade_out * SR))
    a[:n_in] *= np.linspace(0, 1, n_in)[:, None]
    a[-n_out:] *= np.linspace(1, 0, n_out)[:, None]
    write_wav(out, a)
    return 20 * np.log10(gain)


# ---------------------------------------------------------------------------------------------
# artwork
def gradient_bg(c1, c2, glow=None):
    img = Image.new("RGB", (W, H))
    top, bottom = np.array(hex_rgb(c1), float), np.array(hex_rgb(c2), float)
    t = np.linspace(0, 1, H)[:, None]
    rows = (top * (1 - t) + bottom * t).astype(np.uint8)
    img = Image.fromarray(np.repeat(rows[:, None, :], W, axis=1))
    if glow:
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        d.ellipse((W * 0.15, -H * 0.4, W * 0.85, H * 0.6), fill=hex_rgb(glow) + (70,))
        layer = layer.filter(ImageFilter.GaussianBlur(160))
        img = Image.alpha_composite(img.convert("RGBA"), layer).convert("RGB")
    # soft vignette
    vig = Image.new("L", (W, H), 0)
    ImageDraw.Draw(vig).ellipse((-W * 0.25, -H * 0.35, W * 1.25, H * 1.35), fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(220))
    dark = Image.new("RGB", (W, H), (0, 0, 0))
    return Image.composite(img, dark, vig.point(lambda v: 90 + v * 165 // 255))


def plugin_box(meta):
    aspect = meta["width"] / meta["editorHeight"]
    h = PLUGIN_H
    w = int(round(h * aspect / 2) * 2)
    if w > W - 120:
        w = (W - 120) // 2 * 2
        h = int(round(w / aspect / 2) * 2)
    x = (W - w) // 2 // 2 * 2
    return x, PLUGIN_TOP, w, h


def segment_background(seg, box, path):
    img = gradient_bg(seg["colors"][0], seg["colors"][1], seg.get("accent")).convert("RGBA")
    x, y, w, h = box
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((x + 6, y + 22, x + w - 6, y + h + 26), radius=28, fill=(0, 0, 0, 150))
    shadow = shadow.filter(ImageFilter.GaussianBlur(26))
    img = Image.alpha_composite(img, shadow)
    img.convert("RGB").save(path)


def rounded_mask(w, h, r, path):
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), radius=r, fill=255)
    m.save(path)


def caption_image(seg, index, total, path):
    """Transparent PNG with the title (display font) + tagline + small chips."""
    img = Image.new("RGBA", (W, H - CAPTION_Y), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    title_font = font(seg["font"], seg.get("titleSize", 66))
    tag_font = font("Poppins-Medium.ttf", 36)
    title, tag = seg["title"], seg["tagline"]
    gap = 26
    tw = d.textlength(title, font=title_font)
    gw = d.textlength(tag, font=tag_font)
    total_w = tw + gap + 4 + gap + gw
    x0 = (W - total_w) / 2
    cy = (H - CAPTION_Y) / 2 - 6
    accent = hex_rgb(seg.get("titleColor", seg["accent"]))
    d.text((x0, cy), title, font=title_font, fill=accent + (255,), anchor="lm",
           stroke_width=seg.get("titleStroke", 0), stroke_fill=hex_rgb(seg.get("strokeColor", "#1a1a2e")) + (255,))
    dot_x = x0 + tw + gap
    d.ellipse((dot_x, cy - 3, dot_x + 7, cy + 4), fill=(255, 255, 255, 170))
    d.text((dot_x + 4 + gap, cy + 2), tag, font=tag_font, fill=hex_rgb(seg.get("taglineColor", "#ffffff")) + (235,), anchor="lm")

    chip_font = font("Poppins-Bold.ttf", 20)
    kind = seg.get("kind", "").upper()
    if kind:
        kw = d.textlength(kind, font=chip_font) + 28
        d.rounded_rectangle((48, cy - 18, 48 + kw, cy + 18), radius=18, fill=(0, 0, 0, 90))
        d.text((48 + kw / 2, cy + 1), kind, font=chip_font, fill=(255, 255, 255, 220), anchor="mm")
    counter = f"{index} / {total}"
    d.text((W - 48, cy + 1), counter, font=chip_font, fill=(255, 255, 255, 190), anchor="rm")
    img.save(path)


def thumbnails(names):
    shots = []
    for n in names:
        p = ROOT / "docs" / "screenshots" / f"{n}.png"
        if p.exists():
            shots.append(Image.open(p).convert("RGB"))
    return shots


def title_card(path, lines, colors, thumbs, thumb_y=700):
    """lines: list of (text, fontname, size, fill(s), y, stroke) -> background image for a title card."""
    img = gradient_bg(colors[0], colors[1], colors[2] if len(colors) > 2 else None).convert("RGBA")
    d = ImageDraw.Draw(img)
    for text, fname, size, fill, y, stroke in lines:
        f = font(fname, size)
        if isinstance(fill, list):  # multicolour letters
            widths = [d.textlength(ch, font=f) for ch in text]
            x = (W - sum(widths)) / 2
            for ch, wch, i in zip(text, widths, range(len(text))):
                col = hex_rgb(fill[i % len(fill)])
                d.text((x, y), ch, font=f, fill=col + (255,), anchor="lm", stroke_width=stroke,
                       stroke_fill=(26, 22, 46, 255))
                x += wch
        else:
            d.text((W / 2, y), text, font=f, fill=hex_rgb(fill) + (255,), anchor="mm", stroke_width=stroke,
                   stroke_fill=(26, 22, 46, 255))
    img.convert("RGB").save(path)

    # each thumbnail as its own RGBA tile (positions returned for staggered overlays)
    tiles = []
    n = len(thumbs)
    if n:
        tw = 196
        gap = 16
        row_w = n * tw + (n - 1) * gap
        x = (W - row_w) // 2
        for i, t in enumerate(thumbs):
            th = int(tw * t.height / t.width)
            tile = t.resize((tw, th), Image.LANCZOS).convert("RGBA")
            mask = Image.new("L", (tw, th), 0)
            ImageDraw.Draw(mask).rounded_rectangle((0, 0, tw - 1, th - 1), radius=12, fill=255)
            tile.putalpha(mask)
            tp = Path(path).with_name(Path(path).stem + f"_t{i}.png")
            tile.save(tp)
            tiles.append((tp, x + i * (tw + gap), thumb_y - th // 2))
    return tiles


# ---------------------------------------------------------------------------------------------
def render_segment(seg, index, total, rec, work):
    name = seg["plugin"]
    sync = json.loads((rec / f"{name}.sync.json").read_text())
    box = plugin_box(sync)
    x, y, w, h = box
    bg, mask, cap = work / f"{name}_bg.png", work / f"{name}_mask.png", work / f"{name}_cap.png"
    segment_background(seg, box, bg)
    rounded_mask(w, h, 22, mask)
    caption_image(seg, index, total, cap)

    dur = float(seg.get("clipDuration", 6.0))
    start = float(seg.get("clipStart", 2.0))
    gain = prepare_audio(rec / f"{name}.wav", start, dur, work / f"{name}_audio.wav",
                         target_rms_db=seg.get("targetRms", -17.0))

    t0 = sync["offset"] + start
    fade = "if(lt(t,0.15),0,min(1,(t-0.15)/0.45))"
    filt = (
        f"[1:v]crop={sync['width']}:{sync['editorHeight']}:0:0,scale={w}:{h}:flags=lanczos,format=rgba[p];"
        f"[2:v]format=gray[m];[p][m]alphamerge[pm];"
        f"[0:v][pm]overlay={x}:{y}:shortest=1[v1];"
        f"[3:v]format=rgba,colorchannelmixer=aa=1[c];"
        f"[c]fade=t=in:st=0.15:d=0.45:alpha=1[cf];"
        f"[v1][cf]overlay=0:{CAPTION_Y}:shortest=0,format=yuv420p[v]"
    )
    out = work / f"part_{index:02d}_{name}.mp4"
    run(["ffmpeg", "-v", "error", "-y",
         "-loop", "1", "-framerate", str(FPS), "-t", f"{dur}", "-i", str(bg),
         "-ss", f"{t0:.3f}", "-t", f"{dur}", "-i", str(rec / f"{name}.mkv"),
         "-loop", "1", "-framerate", str(FPS), "-t", f"{dur}", "-i", str(mask),
         "-loop", "1", "-framerate", str(FPS), "-t", f"{dur}", "-i", str(cap),
         "-i", str(work / f"{name}_audio.wav"),
         "-filter_complex", filt, "-map", "[v]", "-map", "4:a",
         "-r", str(FPS), "-t", f"{dur}", "-c:v", "libx264", "-preset", "medium", "-crf", "16",
         "-c:a", "aac", "-b:a", "192k", "-ar", str(SR), str(out)])
    print(f"  segment {index}: {name} ({dur:.1f}s, audio gain {gain:+.1f} dB)")
    return out, dur


def render_card(kind, spec, names, work):
    dur = float(spec["seconds"])
    thumbs = thumbnails(names)
    if kind == "intro":
        lines = [
            ("THE ADVENTURE PACK", "LuckiestGuy-Regular.ttf", 128,
             ["#ff5a5f", "#ffb830", "#3ddc97", "#9b7bff", "#ff7eb6", "#5ec8ff"], 330, 7),
            (spec.get("subtitle", "9 whimsical plugins for Ableton Live"), "Poppins-Bold.ttf", 50, "#fff4dc", 470, 0),
        ]
        colors = spec.get("colors", ["#16122a", "#2e2152", "#7a4fd0"])
    else:
        lines = [
            (spec.get("headline", "Free & open source"), "LuckiestGuy-Regular.ttf", 104,
             ["#ffb830", "#ff7eb6", "#5ec8ff", "#3ddc97"], 300, 6),
            (spec.get("url", "github.com/ericrius1/abletonadventures"), "Poppins-Bold.ttf", 54, "#ffd25e", 430, 0),
            (spec.get("detail", "VST3 + AU  ·  macOS + Windows  ·  made for Ableton Live"), "Poppins-Medium.ttf", 34, "#e9e3ff", 505, 0),
        ]
        colors = spec.get("colors", ["#16122a", "#2e2152", "#ff7eb6"])
    bg = work / f"{kind}_bg.png"
    tiles = title_card(bg, lines, colors, thumbs, thumb_y=spec.get("thumbY", 760))

    inputs = ["-loop", "1", "-framerate", str(FPS), "-t", f"{dur}", "-i", str(bg)]
    filt = "[0:v]format=rgba,fade=t=in:st=0:d=0.5[b0];"
    last = "b0"
    for i, (tp, tx, ty) in enumerate(tiles):
        inputs += ["-loop", "1", "-framerate", str(FPS), "-t", f"{dur}", "-i", str(tp)]
        st = 0.5 + 0.11 * i if kind == "intro" else 0.15 + 0.05 * i
        filt += f"[{i + 1}:v]format=rgba,fade=t=in:st={st:.2f}:d=0.35:alpha=1[t{i}];"
        filt += f"[{last}][t{i}]overlay={tx}:{ty}[o{i}];"
        last = f"o{i}"
    filt += f"[{last}]format=yuv420p[v]"

    audio_idx = len(tiles) + 1
    prepare_audio(spec["audio"], float(spec.get("audioStart", 0)), dur, work / f"{kind}_audio.wav",
                  target_rms_db=spec.get("targetRms", -17.0), fade_in=0.05 if kind == "intro" else 0.01,
                  fade_out=dur * 0.6 if kind == "outro" else 0.01)
    inputs += ["-i", str(work / f"{kind}_audio.wav")]
    out = work / f"part_{'00' if kind == 'intro' else '99'}_{kind}.mp4"
    run(["ffmpeg", "-v", "error", "-y", *inputs, "-filter_complex", filt, "-map", "[v]", "-map", f"{audio_idx}:a",
         "-r", str(FPS), "-t", f"{dur}", "-c:v", "libx264", "-preset", "medium", "-crf", "16",
         "-c:a", "aac", "-b:a", "192k", "-ar", str(SR), str(out)])
    print(f"  {kind} card ({dur:.1f}s)")
    return out, dur


def concat(parts, transition, out, transitions=None):
    """Chains parts with xfade / acrossfade."""
    inputs = []
    for p, _ in parts:
        inputs += ["-i", str(p)]
    filt = ""
    vlast, alast = "0:v", "0:a"
    t = parts[0][1]
    for i in range(1, len(parts)):
        kind = (transitions or {}).get(i, "fade")
        off = t - transition
        filt += f"[{vlast}][{i}:v]xfade=transition={kind}:duration={transition}:offset={off:.3f}[v{i}];"
        filt += f"[{alast}][{i}:a]acrossfade=d={transition}:c1=qsin:c2=qsin[a{i}];"
        vlast, alast = f"v{i}", f"a{i}"
        t = off + parts[i][1]
    filt = filt.rstrip(";")
    run(["ffmpeg", "-v", "error", "-y", *inputs, "-filter_complex", filt, "-map", f"[{vlast}]", "-map", f"[{alast}]",
         "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p", "-profile:v", "high",
         "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", str(out)])
    return t


def main():
    cut = json.loads(Path(sys.argv[1]).read_text())
    out = Path(sys.argv[2])
    rec = Path(sys.argv[sys.argv.index("--rec") + 1]) if "--rec" in sys.argv else Path("/home/user/video/rec")
    work = Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else Path("/home/user/video/work")
    work.mkdir(parents=True, exist_ok=True)

    segs = cut["segments"]
    names = [s["plugin"] for s in segs]
    parts = []
    if cut.get("intro"):
        parts.append(render_card("intro", cut["intro"], names, work))
    for i, seg in enumerate(segs, 1):
        parts.append(render_segment(seg, i, len(segs), rec, work))
    if cut.get("outro"):
        parts.append(render_card("outro", cut["outro"], names, work))

    transitions = {int(k): v for k, v in cut.get("transitions", {}).items()}
    total = concat(parts, float(cut.get("transition", 0.35)), out, transitions)
    print(f"wrote {out} ({total:.1f}s)")


if __name__ == "__main__":
    main()
