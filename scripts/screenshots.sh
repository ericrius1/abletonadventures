#!/usr/bin/env bash
# Regenerates docs/screenshots/*.png (and a montage) by opening every plugin in the test harness
# with live audio running. Linux only; needs the plugins built via scripts/dev.sh and ImageMagick.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST="$ROOT/docs/screenshots"
mkdir -p "$DEST"

shoot() {
    local name=$1; shift
    if "$ROOT/scripts/dev.sh" "$name" gui --seconds 6 "$@" > /dev/null 2>&1 && [ -f "/home/user/out/$name/screenshot.png" ]; then
        cp "/home/user/out/$name/screenshot.png" "$DEST/$name.png"
        echo "  ✓ $name"
    else
        echo "  ✗ $name (not built?)"
    fi
}

shoot Stardust --midi chords
shoot CritterKit --midi drums
shoot Orrery --midi none
shoot Babble --midi chords
shoot Dandelion --midi chords
shoot Boing --input drums
shoot Gremlin --input drums
shoot TapeDreams --input pluck
shoot CrystalCave --input pluck

cd "$DEST"
shots=()
for n in Stardust CritterKit Orrery Babble Dandelion Boing Gremlin TapeDreams CrystalCave; do
    [ -f "$n.png" ] && shots+=("$n.png")
done
if [ ${#shots[@]} -gt 0 ]; then
    montage "${shots[@]}" -resize 600x400 -tile 3x -geometry +10+10 -background '#14121c' montage.png
    echo "  ✓ montage.png"
fi
