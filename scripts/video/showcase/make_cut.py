"""Writes /home/user/video/cut.json from segments.json + overrides.json.

Every segment gets an X = 0.4 s pre-roll (clipStart - X, duration + X) so that, with the X-second crossfade, its
first downbeat lands exactly where the previous segment's next downbeat would have been: the 120 BPM grid stays
continuous over the whole video. X = 0.4 s is exactly 12 frames, so every cut also starts on a whole frame and the
picture stays frame-exact against the sample-exact audio.

  python3 make_cut.py [Plugin,Plugin,...]      (order defaults to overrides["_order"])
"""
import json, sys
X = 0.4
segs = json.load(open("/home/user/video/final_tools/segments.json"))
by = {s["plugin"]: s for s in segs}
over = json.load(open("/home/user/video/final_tools/overrides.json"))
order = sys.argv[1].split(",") if len(sys.argv) > 1 else over["_order"]
keys = ["plugin", "title", "tagline", "kind", "font", "titleColor", "titleStroke", "accent", "colors"]
out = []
for p in order:
    s = by[p]
    seg = {k: s[k] for k in keys}
    seg["clipStart"] = round(s["clipStart"] - X, 3)
    seg["clipDuration"] = round(s["clipDuration"] + X, 3)
    seg.update(over.get(p, {}))  # an override clipStart is the final value (e.g. TapeDreams 3.6 + 13 ms latency)
    out.append(seg)
cut = {
    "intro": {"seconds": 4, "audio": "/home/user/video/feeds/band.wav", "audioStart": 0,
              "subtitle": "9 whimsical plugins for Ableton Live", **over.get("_intro", {})},
    "transition": X,
    "segments": out,
    "outro": {"seconds": 4, "audio": "/home/user/video/feeds/band.wav", "audioStart": round(8.0 - X, 3),
              "headline": "Free & open source", "url": "github.com/ericrius1/abletonadventures",
              "detail": "VST3 + AU  ·  macOS · Windows · Linux  ·  made for Ableton Live", **over.get("_outro", {})},
}
if "_transitions" in over:
    cut["transitions"] = over["_transitions"]
if "_audioCurves" in over:
    cut["audioCurves"] = over["_audioCurves"]
json.dump(cut, open("/home/user/video/cut.json", "w"), indent=2, ensure_ascii=False)
n = len(out)
total = cut["intro"]["seconds"] + sum(s["clipDuration"] for s in out) + cut["outro"]["seconds"] - X * (n + 1)
print("order:", " > ".join(order), f"| expected length {total:.2f}s")
