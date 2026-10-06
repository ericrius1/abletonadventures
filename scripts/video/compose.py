#!/usr/bin/env python3
"""Composes the showcase video from recorded segments (see scripts/video/record.py).

  compose.py cut.json out.mp4 [--rec /home/user/video/rec] [--work /home/user/video/work]

How it works:
  * every intermediate part is lossless RGB (libx264rgb -qp 0); the transitions are composed in numpy and the
    final encode converts RGB -> BT.709 yuv420p once and tags the stream as BT.709
  * audio is crossfaded sample-exactly from the parts' WAVs (no AAC padding drift); per-transition curves
  * picture sync: the recording is cut on the nearest frame and re-timed to start at 0, and every boundary sits on
    a whole frame when the transition is a whole number of frames (0.4 s = 12 frames)
  * plugin windows are never upscaled, JUCE's resize grip is painted over, captions sit higher and larger
  * dithered background gradients, animated title cards rendered frame by frame (no fades from black)

cut.json (all segment keys optional except plugin/title/font):
{
  "intro":  {"seconds": 4, "audio": "...wav", "audioStart": 0, "subtitle": "...", "targetLufs": -15},
  "outro":  {"seconds": 4, "audio": "...wav", "pieces": [[7.6, 10.0], [0.0, 2.0]], "headline": "...",
             "url": "...", "detail": "...", "fadeOut": 2.0, "pictureFadeOut": 1.0},
  "transition": 0.4,
  "transitions": {"1": "fade", "2": "pixelize", "3": "push", "10": "flash"},   # incoming part index
  "audioCurves": {"1": "linear"},                                              # default equal-power
  "segments": [
    {"plugin": "Boing", "title": "BOING!", "tagline": "...", "kind": "effect", "font": "LuckiestGuy-Regular.ttf",
     "titleColor": "#FF5A5F", "titleStroke": 4, "accent": "#8FD3FF", "colors": ["#3D8BEA", "#3B2A8E"],
     "clipStart": 3.6, "clipDuration": 6.4, "targetLufs": -15.5, "balanceDb": 1.0,
     "envelope": [[0, 0], [6.4, 1.5]], "thumbAt": 7.0}
  ]
}
"""
import json, subprocess, sys, wave
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageOps
from scipy.ndimage import gaussian_filter, uniform_filter1d, zoom
from scipy.signal import lfilter

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "assets" / "fonts"
W, H, FPS = 1920, 1080, 30
SR = 48000
PLUGIN_H = 840           # max plugin height on screen (840 px editors are shown 1:1, taller ones scaled down)
PLUGIN_TOP = 36
CAPTION_Y = 896
GRIP = 16                # JUCE resize-grip square in the editor's bottom-right corner
RNG = np.random.default_rng(20261006)
LOSSLESS = ["-c:v", "libx264rgb", "-qp", "0", "-preset", "ultrafast", "-pix_fmt", "rgb24"]


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
# audio
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
    s = s.reshape(-1, ch)
    return np.repeat(s, 2, axis=1) if ch == 1 else s


def write_wav(path, audio):
    x = np.clip(np.round(np.clip(audio, -1, 1) * (2 ** 23 - 1)), -2 ** 23, 2 ** 23 - 1).astype(np.int32)
    b = np.zeros((x.size, 3), np.uint8)
    xf = x.reshape(-1) & 0xFFFFFF
    b[:, 0], b[:, 1], b[:, 2] = xf & 255, (xf >> 8) & 255, (xf >> 16) & 255
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(3)
        w.setframerate(SR)
        w.writeframes(b.tobytes())


def kweight(x):
    x = lfilter([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585], x, axis=0)
    return lfilter([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621], x, axis=0)


def lufs(a):
    """Gated integrated loudness (BS.1770): 400 ms blocks, 75 % overlap."""
    k = kweight(a)
    blk, hop = int(0.4 * SR), int(0.1 * SR)
    ms = np.array([np.sum(np.mean(k[i:i + blk] ** 2, axis=0)) for i in range(0, max(1, len(k) - blk + 1), hop)])
    l = -0.691 + 10 * np.log10(ms + 1e-12)
    g = ms[l > -70]
    if not len(g):
        return -70.0
    rel = -0.691 + 10 * np.log10(np.mean(g)) - 10
    return -0.691 + 10 * np.log10(np.mean(ms[(l > -70) & (l > rel)]))


def envelope_db(n, points):
    """Piecewise-linear gain curve in dB from [[t, dB], ...] (seconds from the part start)."""
    t = np.arange(n) / SR
    ts, ds = zip(*sorted(points))
    return 10 ** (np.interp(t, ts, ds) / 20)


def prepare_audio(src, start, seconds, out, spec, fade_in=0.005, fade_out=0.005, fade_shape="linear"):
    """Cuts [start, start+seconds] (or the spec's "pieces") from src, shapes and loudness-normalises it."""
    audio = read_wav(src)
    n = int(round(seconds * SR))
    if spec.get("pieces"):
        chunks = []
        for s0, s1 in spec["pieces"]:
            c = audio[int(round(s0 * SR)): int(round(s1 * SR))].copy()
            j = int(0.006 * SR)  # tiny fades at the splice points (they land on bar lines)
            c[:j] *= np.linspace(0, 1, j)[:, None]
            c[-j:] *= np.linspace(1, 0, j)[:, None]
            chunks.append(c)
        a = np.vstack(chunks)
    else:
        a = audio[int(round(start * SR)): int(round(start * SR)) + n].copy()
    if len(a) < n:
        a = np.vstack([a, np.zeros((n - len(a), 2))])
    a = a[:n]
    if spec.get("envelope"):
        a *= envelope_db(n, spec["envelope"])[:, None]
    if spec.get("balanceDb"):
        b = float(spec["balanceDb"])  # > 0 moves the image right
        a[:, 0] *= 10 ** (-b / 40)
        a[:, 1] *= 10 ** (b / 40)
    if "targetLufs" in spec:
        gain = 10 ** ((spec["targetLufs"] - lufs(a)) / 20)
    else:
        rms = np.sqrt(np.mean(a.mean(axis=1) ** 2)) + 1e-9
        gain = 10 ** (spec.get("targetRms", -17.0) / 20) / rms
    peak = np.max(np.abs(a)) + 1e-9
    gain = min(gain, 10 ** (-1.5 / 20) / peak)
    a = a * gain
    n_in, n_out = max(1, int(fade_in * SR)), max(1, int(fade_out * SR))
    a[:n_in] *= np.linspace(0, 1, n_in)[:, None]
    r = np.linspace(1, 0, n_out)
    if fade_shape == "cos":
        r = 0.5 + 0.5 * np.cos(np.pi * (1 - r))
    a[-n_out:] *= r[:, None]
    write_wav(out, a)
    return 20 * np.log10(gain)


def mix_audio(parts, transition, out, curves=None):
    """Sample-exact crossfades of the parts' WAVs: equal-power by default, "linear" (equal-gain) for transitions
    between correlated material (the same groove on both sides)."""
    n_x = int(round(transition * SR))
    starts, t = [0], parts[0]["dur"]
    for p in parts[1:]:
        off = t - transition
        starts.append(int(round(off * SR)))
        t = off + p["dur"]
    total = int(round(t * SR))
    mix = np.zeros((total + SR, 2))
    x = np.linspace(0, 1, n_x)
    ramps = {"power": np.sin(x * np.pi / 2), "linear": x}
    for i, p in enumerate(parts):
        a = read_wav(p["wav"])[: int(round(p["dur"] * SR))].copy()
        if i > 0:
            a[:n_x] *= ramps[(curves or {}).get(i, "power")][:, None]
        if i < len(parts) - 1:
            a[-n_x:] *= ramps[(curves or {}).get(i + 1, "power")][::-1, None]
        mix[starts[i]: starts[i] + len(a)] += a
    mix = mix[:total]
    peak = np.max(np.abs(mix)) + 1e-9
    if peak > 10 ** (-1.5 / 20):
        mix *= 10 ** (-1.5 / 20) / peak
    write_wav(out, mix)
    return t


# ---------------------------------------------------------------------------------------------
# artwork (float pipelines, dithered once at the end so the dark gradients don't band)
def _blurred_ellipse(box, sigma):
    s = 4
    yy, xx = np.mgrid[0:H // s, 0:W // s].astype(np.float32) * s + s / 2
    x0, y0, x1, y1 = box
    cx, cy, rx, ry = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    m = ((((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2) <= 1).astype(np.float32)
    m = gaussian_filter(m, sigma / s, mode="nearest")
    return np.clip(zoom(m, s, order=1)[:H, :W], 0, 1)


def gradient_float(c1, c2, glow=None):
    top, bottom = np.array(hex_rgb(c1), np.float32), np.array(hex_rgb(c2), np.float32)
    t = np.linspace(0, 1, H, dtype=np.float32)[:, None, None]
    img = np.broadcast_to(top * (1 - t) + bottom * t, (H, W, 3)).astype(np.float32)
    if glow:
        m = _blurred_ellipse((W * 0.15, -H * 0.4, W * 0.85, H * 0.6), 160)[..., None] * (70 / 255)
        img = img * (1 - m) + np.array(hex_rgb(glow), np.float32) * m
    vig = _blurred_ellipse((-W * 0.25, -H * 0.35, W * 1.25, H * 1.35), 220)[..., None]
    return img * ((90 + vig * 165) / 255)


def dither(img):
    """float RGB -> uint8 with static TPDF dither (breaks up 8-bit stepping in the dark gradients)."""
    noise = RNG.random(img.shape, dtype=np.float32) - RNG.random(img.shape, dtype=np.float32)
    return np.clip(np.round(img + noise), 0, 255).astype(np.uint8)


def plugin_box(meta):
    ew, eh = meta["width"], meta["editorHeight"]
    h = min(PLUGIN_H, eh)
    w = ew if h == eh else int(round(ew * h / eh / 2) * 2)
    if w > W - 120:
        w = (W - 120) // 2 * 2
        h = int(round(w * eh / ew / 2) * 2)
    return (W - w) // 2, PLUGIN_TOP, w, h


def segment_background(seg, box, path):
    img = gradient_float(seg["colors"][0], seg["colors"][1], seg.get("accent"))
    x, y, w, h = box
    sh = Image.new("L", (W, H), 0)
    ImageDraw.Draw(sh).rounded_rectangle((x + 6, y + 22, x + w - 6, y + h + 26), radius=40, fill=255)
    m = gaussian_filter(np.asarray(sh, np.float32) / 255, 26)[..., None] * (150 / 255)
    Image.fromarray(dither(img * (1 - m))).save(path)


def rounded_mask(w, h, r, path):
    s = 4  # supersampled, so the corners are anti-aliased
    m = Image.new("L", (w * s, h * s), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w * s - 1, h * s - 1), radius=r * s, fill=255)
    m.resize((w, h), Image.LANCZOS).save(path)


def text_shadow(layer, radius=6, alpha=110):
    """Soft dark halo behind the text of an RGBA layer (keeps light captions legible on light backgrounds)."""
    a = np.asarray(layer.split()[-1], np.float32)
    a = gaussian_filter(a, radius) * (alpha / 255)
    sh = Image.new("RGBA", layer.size, (10, 6, 24, 0))
    sh.putalpha(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)))
    return Image.alpha_composite(sh, layer)


def caption_image(seg, path):
    """Transparent PNG: [KIND chip]  Title  ·  tagline, centred on one line."""
    ch = H - CAPTION_Y
    img = Image.new("RGBA", (W, ch), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    title, tag, kind = seg["title"], seg["tagline"], seg.get("kind", "").upper()
    tsize, gsize, gap = seg.get("titleSize", 76), 44, 26
    chip_font = font("Poppins-Bold.ttf", 26)
    kw = d.textlength(kind, font=chip_font) + 36 if kind else 0
    while True:
        title_font, tag_font = font(seg["font"], tsize), font("Poppins-Medium.ttf", gsize)
        tw, gw = d.textlength(title, font=title_font), d.textlength(tag, font=tag_font)
        total_w = (kw + gap if kind else 0) + tw + gap + 7 + gap + gw
        if total_w <= 1640 or tsize <= 56:
            break
        tsize, gsize = tsize - 2, max(38, gsize - 1)
    x0 = (W - total_w) / 2
    cy = ch / 2 - 10
    if kind:
        d.rounded_rectangle((x0, cy - 22, x0 + kw, cy + 22), radius=22, fill=(0, 0, 0, 150),
                            outline=(255, 255, 255, 70), width=2)
        d.text((x0 + kw / 2, cy + 1), kind, font=chip_font, fill=(255, 255, 255, 235), anchor="mm")
        x0 += kw + gap
    accent = hex_rgb(seg.get("titleColor", seg["accent"]))
    d.text((x0, cy), title, font=title_font, fill=accent + (255,), anchor="lm",
           stroke_width=seg.get("titleStroke", 0), stroke_fill=hex_rgb(seg.get("strokeColor", "#1a1a2e")) + (255,))
    dot_x = x0 + tw + gap
    d.ellipse((dot_x, cy - 3, dot_x + 7, cy + 4), fill=(255, 255, 255, 190))
    d.text((dot_x + 7 + gap, cy + 2), tag, font=tag_font, fill=hex_rgb(seg.get("taglineColor", "#ffffff")) + (240,), anchor="lm")
    text_shadow(img).save(path)


def sync_time(rec, name):
    """Recording time (pts) of audio time 0. record.py finds the sync strip by frame *index* (index / 30); the
    screen grabber can drop a frame now and then (Boing's take drops one before the sync point, Dandelion's two
    inside the clip), so convert that index to the frame's real timestamp and cut by timestamps."""
    sync = json.loads((rec / f"{name}.sync.json").read_text())
    r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v", "-show_entries", "frame=pts_time",
                        "-of", "csv=p=0", str(rec / f"{name}.mkv")], capture_output=True, text=True)
    pts = [float(l.strip().rstrip(",")) for l in r.stdout.splitlines() if l.strip()]
    return pts[int(round(sync["offset"] * FPS))]


def grab_editor_img(rec, name, t_audio):
    sync = json.loads((rec / f"{name}.sync.json").read_text())
    ew, eh = sync["width"], sync["editorHeight"]
    r = subprocess.run(["ffmpeg", "-v", "error", "-ss", f"{sync_time(rec, name) + t_audio:.3f}", "-i", str(rec / f"{name}.mkv"),
                        "-frames:v", "1", "-vf", f"crop={ew}:{eh}:0:0,scale=in_color_matrix=bt601:in_range=tv,format=rgb24",
                        "-f", "rawvideo", "-"], capture_output=True)
    a = np.frombuffer(r.stdout, np.uint8)[: ew * eh * 3].reshape(eh, ew, 3).copy()
    a[eh - GRIP:, ew - GRIP:] = a[eh - GRIP:, ew - 2 * GRIP: ew - GRIP][:, ::-1]
    return Image.fromarray(a)


# ---------------------------------------------------------------------------------------------
def render_segment(seg, index, rec, work, caption_fade):
    name = seg["plugin"]
    sync = json.loads((rec / f"{name}.sync.json").read_text())
    ew, eh = sync["width"], sync["editorHeight"]
    box = plugin_box(sync)
    x, y, w, h = box
    bg, mask, cap = work / f"{name}_bg.png", work / f"{name}_mask.png", work / f"{name}_cap.png"
    segment_background(seg, box, bg)
    rounded_mask(w, h, 24, mask)
    caption_image(seg, cap)

    dur = float(seg.get("clipDuration", 6.0))
    start = float(seg.get("clipStart", 2.0))
    gain = prepare_audio(rec / f"{name}.wav", start, dur, work / f"{name}_audio.wav", seg)

    # Take the recording frame nearest to the clip start, then snap every frame's timestamp to the 1/30 s grid:
    # the mkv's millisecond-rounded timestamps (0.033, 0.067...) would otherwise make overlay repeat one frame and
    # skip the next in every group of three, while snapping (not renumbering) keeps a dropped grab where it was.
    # Part frame j then shows the recording frame that belongs to audio time clipStart + j/30.
    t0 = sync_time(rec, name) + start
    scale = "" if (w, h) == (ew, eh) else f",scale={w}:{h}:flags=lanczos+accurate_rnd"
    cap_f = f"fade=t=in:st={caption_fade[0]}:d={caption_fade[1]}:alpha=1," if caption_fade else ""
    filt = (
        f"[1:v]setpts=round((PTS-STARTPTS)*TB*{FPS})/({FPS}*TB),crop={ew}:{eh}:0:0,"
        f"scale=in_color_matrix=bt601:in_range=tv:flags=accurate_rnd+full_chroma_int,format=rgb24,split[e0][e1];"
        f"[e1]crop={GRIP}:{GRIP}:{ew - 2 * GRIP}:{eh - GRIP},hflip[g];"
        f"[e0][g]overlay={ew - GRIP}:{eh - GRIP}:format=rgb{scale},format=rgba[p];"
        f"[2:v]format=gray[m];[p][m]alphamerge[pm];"
        f"[0:v]format=rgb24[b];[b][pm]overlay={x}:{y}:format=rgb:shortest=1[v1];"
        f"[3:v]format=rgba,{cap_f}null[c];"
        f"[v1][c]overlay=0:{CAPTION_Y}:format=rgb:shortest=0,format=rgb24[v]"
    )
    out = work / f"part_{index:02d}_{name}.mkv"
    run(["ffmpeg", "-v", "error", "-y",
         "-loop", "1", "-framerate", str(FPS), "-t", f"{dur}", "-i", str(bg),
         "-ss", f"{t0 - 0.5 / FPS:.4f}", "-t", f"{dur + 0.2}", "-i", str(rec / f"{name}.mkv"),
         "-loop", "1", "-framerate", str(FPS), "-t", f"{dur}", "-i", str(mask),
         "-loop", "1", "-framerate", str(FPS), "-t", f"{dur}", "-i", str(cap),
         "-filter_complex", filt, "-map", "[v]", "-r", str(FPS), "-frames:v", str(int(round(dur * FPS))),
         *LOSSLESS, str(out)])
    print(f"  segment {index}: {name} ({dur:.2f}s, audio gain {gain:+.1f} dB)")
    return {"video": out, "dur": dur, "wav": work / f"{name}_audio.wav"}


# ---------------------------------------------------------------------------------------------
# title cards, rendered frame by frame
def ease_out_back(u, s=1.9):
    u = np.clip(u, 0, 1) - 1
    return 1 + (s + 1) * u ** 3 + s * u ** 2


def rounded_tile(img, size, radius=14, border=(255, 255, 255, 60)):
    t = ImageOps.fit(img, size, Image.LANCZOS, centering=(0.5, 0.25)).convert("RGBA")
    s = 4
    m = Image.new("L", (size[0] * s, size[1] * s), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, size[0] * s - 1, size[1] * s - 1), radius=radius * s, fill=255)
    t.putalpha(m.resize(size, Image.LANCZOS))
    ring = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(ring).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius=radius, outline=border, width=2)
    t = Image.alpha_composite(t, ring)
    pad = 40  # drop shadow
    full = (size[0] + 2 * pad, size[1] + 2 * pad)
    sh = Image.new("L", full, 0)
    ImageDraw.Draw(sh).rounded_rectangle((pad, pad + 10, pad + size[0], pad + size[1] + 10), radius=radius, fill=150)
    sh = sh.filter(ImageFilter.GaussianBlur(14))
    shadow = Image.new("RGBA", full, (8, 4, 20, 0))
    shadow.putalpha(sh)
    shadow.alpha_composite(t, (pad, pad))
    return shadow, pad


def letter_sprites(text, fname, size, colors, stroke):
    f = font(fname, size)
    d = ImageDraw.Draw(Image.new("RGBA", (8, 8)))
    widths = [d.textlength(ch, font=f) for ch in text]
    sprites = []
    for i, ch in enumerate(text):
        if ch == " ":
            sprites.append(None)
            continue
        im = Image.new("RGBA", (int(widths[i]) + 4 * stroke + 40, size * 2), (0, 0, 0, 0))
        ImageDraw.Draw(im).text((2 * stroke + 20, size), ch, font=f, fill=hex_rgb(colors[i % len(colors)]) + (255,),
                                anchor="lm", stroke_width=stroke, stroke_fill=(26, 22, 46, 255))
        sprites.append(im)
    return sprites, widths, 2 * stroke + 20, size


def render_card(kind, spec, segs, rec, work):
    dur = float(spec["seconds"])
    n = int(round(dur * FPS))
    names = [s["plugin"] for s in segs]
    if kind == "intro":
        colors = spec.get("colors", ["#16122a", "#2e2152", "#7a4fd0"])
        title = ("THE ADVENTURE PACK", "LuckiestGuy-Regular.ttf", 128,
                 ["#ff5a5f", "#ffb830", "#3ddc97", "#9b7bff", "#ff7eb6", "#5ec8ff"], 7, spec.get("titleY", 262))
        lines = [(spec.get("subtitle", "9 free, whimsical plugins for Ableton Live"), "Poppins-Bold.ttf", 52, "#fff4dc",
                  spec.get("subtitleY", 388))]
        tile_w, rows_y = 300, spec.get("thumbRows", [598, 828])
        pop0, pop_step, bounce0 = 0.25, 0.25, 0.5
        hop = True   # tiles are on screen from frame 0 (poster frame) and hop in turn on successive 8ths
    else:
        colors = spec.get("colors", ["#16122a", "#2e2152", "#ff7eb6"])
        title = ("THE ADVENTURE PACK", "LuckiestGuy-Regular.ttf", 92,
                 ["#ff5a5f", "#ffb830", "#3ddc97", "#9b7bff", "#ff7eb6", "#5ec8ff"], 6, spec.get("titleY", 168))
        lines = [(spec.get("headline", "Free & open source"), "Poppins-Bold.ttf", 70, "#fff4dc", 282),
                 (spec.get("url", "github.com/ericrius1/abletonadventures"), "Poppins-Bold.ttf", 58, "#ffd25e", 392),
                 (spec.get("detail", "VST3 + AU  ·  macOS · Windows · Linux  ·  made for Ableton Live"),
                  "Poppins-Medium.ttf", 40, "#e9e3ff", 482)]
        tile_w, rows_y = 250, spec.get("thumbRows", [664, 860])
        pop0, pop_step, bounce0 = 0.4, 0.125, 0.4
        hop = False  # tiles pop in on 16ths from the downbeat

    base = Image.fromarray(dither(gradient_float(colors[0], colors[1], colors[2] if len(colors) > 2 else None))).convert("RGBA")
    static = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(static)
    for text, fname, size, fill, y in lines:
        if kind == "outro" and fname == "Poppins-Bold.ttf" and size == 58:  # URL pill
            f = font(fname, size)
            tw = d.textlength(text, font=f)
            d.rounded_rectangle((W / 2 - tw / 2 - 40, y - 50, W / 2 + tw / 2 + 40, y + 48), radius=48,
                                fill=(10, 6, 30, 150), outline=(255, 210, 94, 150), width=3)
        d.text((W / 2, y), text, font=font(fname, size), fill=hex_rgb(fill) + (255,), anchor="mm")
    static = text_shadow(static, 8, 120)

    text, fname, size, tcolors, stroke, ty = title
    sprites, widths, ox, oy = letter_sprites(text, fname, size, tcolors, stroke)
    lx = (W - sum(widths)) / 2
    xs = [lx + sum(widths[:i]) for i in range(len(widths))]

    # thumbnails: frames from the recordings, 5 + 4 grid, all the same size
    th = int(round(tile_w / 1.55))
    tiles = []
    gap = 28
    for i, s in enumerate(segs):
        img = grab_editor_img(rec, s["plugin"], s.get("thumbAt", s["clipStart"] + 3.4))
        tile, pad = rounded_tile(img, (tile_w, th))
        row = 0 if i < 5 else 1
        cnt = 5 if row == 0 else len(segs) - 5
        col = i if row == 0 else i - 5
        row_w = cnt * tile_w + (cnt - 1) * gap
        cx = (W - row_w) / 2 + col * (tile_w + gap) + tile_w / 2
        tiles.append((tile, pad, cx, rows_y[row]))

    fade_out = float(spec.get("pictureFadeOut", 1.0)) if kind == "outro" else 0.0
    zoom_to = 1.03
    out = work / f"part_{'00' if kind == 'intro' else '99'}_{kind}.mkv"
    enc = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                            "-framerate", str(FPS), "-i", "-", *LOSSLESS, str(out)], stdin=subprocess.PIPE)
    for k in range(n):
        t = k / FPS
        fr = base.copy()
        fr.alpha_composite(static)
        # title letters ripple on every beat (a wave travelling left to right)
        for i, sp in enumerate(sprites):
            if sp is None:
                continue
            dy = 0.0
            for b in np.arange(bounce0, dur, 0.5):
                u = (t - b - 0.012 * i) / 0.26
                if 0 <= u <= 1:
                    dy -= (11 if abs((b - bounce0) % 2.0) < 1e-6 else 7) * np.sin(np.pi * u)
            fr.alpha_composite(sp, (int(round(xs[i] - ox)), int(round(ty - oy + dy))))
        # thumbnails pop in on successive 8ths / 16ths
        for i, (tile, pad, cx, cy) in enumerate(tiles):
            u = (t - (pop0 + pop_step * i)) / 0.32
            lift = 0.0
            if hop:
                al = 1.0
                sc = 1.0 + (0.08 * np.sin(np.pi * u) if 0 < u < 1 else 0.0)
                lift = -12 * np.sin(np.pi * u) if 0 < u < 1 else 0.0
            else:
                if u <= 0:
                    continue
                sc = 0.55 + 0.45 * float(ease_out_back(u))
                al = min(1.0, u / 0.35)
            tw2, th2 = max(2, int(round(tile.width * sc))), max(2, int(round(tile.height * sc)))
            tl = tile if (tw2, th2) == tile.size else tile.resize((tw2, th2), Image.BICUBIC)
            if al < 1:
                a = np.asarray(tl.split()[-1], np.float32) * al
                tl = tl.copy()
                tl.putalpha(Image.fromarray(a.astype(np.uint8)))
            fr.alpha_composite(tl, (int(round(cx - tw2 / 2)), int(round(cy - th2 / 2 + lift))))
        # slow push-in about the centre
        z = 1 + (zoom_to - 1) * (t / dur)
        cx, cy = W / 2, H / 2
        fr = fr.convert("RGB").transform((W, H), Image.AFFINE, (1 / z, 0, cx - cx / z, 0, 1 / z, cy - cy / z),
                                         resample=Image.BICUBIC)
        a = np.asarray(fr)
        if fade_out and t > dur - fade_out:
            g = max(0.0, (dur - t - 1 / FPS) / fade_out)
            a = (a.astype(np.float32) * (g * g * (3 - 2 * g))).round().astype(np.uint8)
        enc.stdin.write(np.ascontiguousarray(a).tobytes())
    enc.stdin.close()
    if enc.wait() != 0:
        sys.exit("card encode failed")

    fo = float(spec.get("fadeOut", dur * 0.6)) if kind == "outro" else 0.005
    prepare_audio(spec["audio"], float(spec.get("audioStart", 0)), dur, work / f"{kind}_audio.wav", spec,
                  fade_in=0.005, fade_out=fo, fade_shape="cos" if kind == "outro" else "linear")
    print(f"  {kind} card ({dur:.1f}s)")
    return {"video": out, "dur": dur, "wav": work / f"{kind}_audio.wav"}


# ---------------------------------------------------------------------------------------------
# transitions (numpy, on RGB frames)
def smoothstep(p):
    p = np.clip(p, 0, 1)
    return p * p * (3 - 2 * p)


def tr_fade(a, b, p):
    return a * (1 - p) + b * p


def tr_push(a, b, p):
    """Eased push to the left. Horizontal motion blur (a box blur 0.4x the distance travelled per frame, i.e. a
    144-degree shutter) keeps the fast middle of the move from strobing."""
    s = int(round(float(smoothstep(p)) * W))
    fr = np.empty(a.shape, np.float32)
    fr[:, : W - s] = a[:, s:]
    fr[:, W - s:] = b[:, :s]
    nx = 12
    v = W * float(smoothstep(p + 0.5 / nx) - smoothstep(p - 0.5 / nx))
    k = int(round(v * 0.4))   # 144-degree shutter
    if k >= 3:
        fr = uniform_filter1d(fr, size=k, axis=1, mode="nearest")
    return fr


def _pixelate(img, blk):
    if blk <= 1:
        return img
    h, w = H // blk, W // blk
    small = img.reshape(h, blk, w, blk, 3).mean(axis=(1, 3))
    return np.repeat(np.repeat(small, blk, axis=0), blk, axis=1)


def tr_pixelize(a, b, p):
    sizes = [1, 4, 8, 12, 20, 30, 40, 60]
    k = int(round((1 - abs(2 * p - 1)) * (len(sizes) - 1)))
    blk = sizes[k]
    m = float(smoothstep((p - 0.4) / 0.2))
    return _pixelate(a, blk) * (1 - m) + _pixelate(b, blk) * m


def tr_flash(a, b, p):
    """Frost flash: dissolve through a pale icy white."""
    frost = np.array([234, 244, 255], np.float32)
    base = a * (1 - smoothstep(p)) + b * smoothstep(p)
    f = 0.8 * np.sin(np.pi * p) ** 3
    return base * (1 - f) + frost * f


TRANSITIONS = {"fade": tr_fade, "push": tr_push, "pixelize": tr_pixelize, "flash": tr_flash}


def read_frames(path):
    p = subprocess.Popen(["ffmpeg", "-v", "error", "-i", str(path), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                         stdout=subprocess.PIPE)
    size = W * H * 3
    while True:
        buf = p.stdout.read(size)
        if len(buf) < size:
            break
        yield np.frombuffer(buf, np.uint8).reshape(H, W, 3)
    p.wait()


def concat(parts, transition, out, transitions, work, curves):
    t = mix_audio(parts, transition, work / "mix.wav", curves)
    nx = int(round(transition * FPS))
    total_frames = int(round(t * FPS))
    enc = subprocess.Popen(
        ["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-framerate", str(FPS),
         "-i", "-", "-i", str(work / "mix.wav"), "-map", "0:v", "-map", "1:a",
         "-vf", "scale=out_color_matrix=bt709:out_range=tv:flags=accurate_rnd,format=yuv420p",
         "-c:v", "libx264", "-preset", "slow", "-crf", "13", "-tune", "animation", "-x264-params", "aq-mode=3",
         "-profile:v", "high", "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709",
         "-color_range", "tv", "-c:a", "aac", "-b:a", "256k", "-ar", str(SR), "-movflags", "+faststart",
         "-t", f"{t:.4f}", str(out)], stdin=subprocess.PIPE)
    written = 0
    tail = []  # last nx frames of the previous part
    for i, p in enumerate(parts):
        n = int(round(p["dur"] * FPS))
        kind = transitions.get(i, "fade")
        fn = TRANSITIONS.get(kind, tr_fade)
        for j, fr in enumerate(read_frames(p["video"])):
            if j >= n:
                break
            if i > 0 and j < nx:
                q = (j + 0.5) / nx
                fr = np.clip(np.round(fn(tail[j].astype(np.float32), fr.astype(np.float32), q)), 0, 255).astype(np.uint8)
                enc.stdin.write(fr.tobytes())
                written += 1
            elif i < len(parts) - 1 and j >= n - nx:
                if j == n - nx:
                    tail = []
                tail.append(fr.copy())
            else:
                enc.stdin.write(fr.tobytes())
                written += 1
        if i < len(parts) - 1 and len(tail) != nx:
            sys.exit(f"part {p['video']} has too few frames")
    enc.stdin.close()
    if enc.wait() != 0:
        sys.exit("final encode failed")
    print(f"  frames written {written} (expected {total_frames})")
    return t


def main():
    cut = json.loads(Path(sys.argv[1]).read_text())
    out = Path(sys.argv[2])
    rec = Path(sys.argv[sys.argv.index("--rec") + 1]) if "--rec" in sys.argv else Path("/home/user/video/rec")
    work = Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else Path("/home/user/video/work")
    work.mkdir(parents=True, exist_ok=True)

    segs = cut["segments"]
    transitions = {int(k): v for k, v in cut.get("transitions", {}).items()}
    curves = {int(k): v for k, v in cut.get("audioCurves", {}).items()}
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    parts = []
    if cut.get("intro"):
        parts.append(render_card("intro", cut["intro"], segs, rec, work) if not only or "intro" in only else
                     {"video": work / "part_00_intro.mkv", "dur": float(cut["intro"]["seconds"]), "wav": work / "intro_audio.wav"})
    for i, seg in enumerate(segs, 1):
        incoming = transitions.get(i, "fade")
        cap_fade = (0.2, 0.4) if incoming in ("fade", "flash") else None
        if not only or seg["plugin"] in only:
            parts.append(render_segment(seg, i, rec, work, cap_fade))
        else:
            parts.append({"video": work / f"part_{i:02d}_{seg['plugin']}.mkv", "dur": float(seg["clipDuration"]),
                          "wav": work / f"{seg['plugin']}_audio.wav"})
    if cut.get("outro"):
        parts.append(render_card("outro", cut["outro"], segs, rec, work) if not only or "outro" in only else
                     {"video": work / "part_99_outro.mkv", "dur": float(cut["outro"]["seconds"]), "wav": work / "outro_audio.wav"})

    total = concat(parts, float(cut.get("transition", 0.4)), out, transitions, work, curves)
    print(f"wrote {out} ({total:.2f}s)")


if __name__ == "__main__":
    main()
