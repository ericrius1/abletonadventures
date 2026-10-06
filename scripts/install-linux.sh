#!/bin/bash
# Adventure Pack installer for Linux: copies the VST3 bundles into ~/.vst3
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)"
DEST="$HOME/.vst3"
mkdir -p "$DEST"
for b in "$SRC"/VST3/*.vst3; do
    rm -rf "$DEST/$(basename "$b")"
    cp -R "$b" "$DEST/"
    echo "  ✓ $(basename "$b")"
done
echo "Installed into $DEST - rescan plug-ins in your DAW."
