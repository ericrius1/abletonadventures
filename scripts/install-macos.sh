#!/bin/bash
# Adventure Pack installer for macOS.
#
# Run from inside the unzipped AdventurePack-macOS folder:   bash install.sh
# ...or straight from the internet (downloads the latest release for you):
#   curl -fsSL https://github.com/ericrius1/abletonadventures/releases/latest/download/install-macos.sh | bash
set -euo pipefail

REPO="ericrius1/abletonadventures"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"

say() { printf '\033[1;35m%s\033[0m\n' "$*"; }

SRC="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" 2> /dev/null && pwd || echo "")"
if [ -z "$SRC" ] || [ ! -d "$SRC/VST3" ]; then
    say "Downloading the latest Adventure Pack release..."
    TMP=$(mktemp -d)
    curl -fL --progress-bar -o "$TMP/pack.zip" "https://github.com/$REPO/releases/latest/download/AdventurePack-macOS.zip"
    ditto -x -k "$TMP/pack.zip" "$TMP"
    SRC="$TMP/AdventurePack-macOS"
fi

mkdir -p "$VST3_DIR" "$AU_DIR"

install_bundle() {
    local bundle="$1" dest_dir="$2"
    local name; name=$(basename "$bundle")
    rm -rf "$dest_dir/$name"
    cp -R "$bundle" "$dest_dir/"
    # Downloads from a browser are quarantined; unsigned plugins then refuse to load. Clear it,
    # and (re)apply an ad-hoc signature so Apple Silicon is happy.
    xattr -dr com.apple.quarantine "$dest_dir/$name" 2> /dev/null || true
    codesign --force --deep --sign - "$dest_dir/$name" > /dev/null 2>&1 || true
    echo "  ✓ $name"
}

say "Installing VST3 plugins into $VST3_DIR"
for b in "$SRC"/VST3/*.vst3; do install_bundle "$b" "$VST3_DIR"; done

if [ -d "$SRC/Components" ]; then
    say "Installing Audio Units into $AU_DIR"
    for b in "$SRC"/Components/*.component; do install_bundle "$b" "$AU_DIR"; done
    # Make macOS notice the new Audio Units straight away.
    killall -9 AudioComponentRegistrar > /dev/null 2>&1 || true
fi

say "All done! 🎉"
cat << 'EOF'

Next, in Ableton Live:
  1. Settings > Plug-Ins: turn on "Use Audio Units v2" and/or "Use VST3 Plug-in System Folders".
  2. Click "Rescan" (hold Option/Alt while clicking for a full rescan).
  3. Find everything under Plug-Ins > "Adventure Audio" in the browser.
EOF
