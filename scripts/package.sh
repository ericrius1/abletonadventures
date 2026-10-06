#!/usr/bin/env bash
# Collects the built plugins into a ready-to-install zip.
#
#   scripts/package.sh <macOS|Windows|Linux> [build-dir]
#
# Produces dist/AdventurePack-<platform>.zip containing the plugins, an installer script and a readme.
set -euo pipefail

PLATFORM=${1:?platform: macOS, Windows or Linux}
BUILD=${2:-build}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(sed -n 's/^project(AdventurePack VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
NAME="AdventurePack-$PLATFORM"
STAGE="$ROOT/dist/$NAME"

rm -rf "$STAGE" "$ROOT/dist/$NAME.zip"
mkdir -p "$STAGE/VST3"

shopt -s nullglob
count=0
for dir in "$BUILD"/plugins/*/*_artefacts/Release; do
    for vst3 in "$dir"/VST3/*.vst3; do
        cp -R "$vst3" "$STAGE/VST3/"
        count=$((count + 1))
    done
    if [ "$PLATFORM" = "macOS" ]; then
        mkdir -p "$STAGE/Components"
        for au in "$dir"/AU/*.component; do
            cp -R "$au" "$STAGE/Components/"
        done
    fi
done

if [ "$count" -eq 0 ]; then
    echo "No plugins found under $BUILD/plugins - did the build run?" >&2
    exit 1
fi

# Strip symbols (smaller downloads) and, on macOS, ad-hoc sign every bundle so Apple Silicon will load it.
case "$PLATFORM" in
    macOS)
        for bundle in "$STAGE"/VST3/*.vst3 "$STAGE"/Components/*.component; do
            for bin in "$bundle"/Contents/MacOS/*; do
                strip -x "$bin" 2> /dev/null || true
            done
            codesign --force --deep --sign - --timestamp=none "$bundle"
        done
        cp "$ROOT/scripts/install-macos.sh" "$STAGE/install.sh"
        cat > "$STAGE/Install Adventure Pack.command" << 'EOF'
#!/bin/bash
cd "$(dirname "$0")" && bash ./install.sh
echo
read -n 1 -s -r -p "Done! Press any key to close this window."
EOF
        chmod +x "$STAGE/install.sh" "$STAGE/Install Adventure Pack.command"
        ;;
    Linux)
        for so in "$STAGE"/VST3/*.vst3/Contents/*/*.so; do
            strip --strip-unneeded "$so" || true
        done
        cp "$ROOT/scripts/install-linux.sh" "$STAGE/install.sh"
        chmod +x "$STAGE/install.sh"
        ;;
    Windows)
        cp "$ROOT/scripts/install-windows.ps1" "$STAGE/install.ps1"
        cp "$ROOT/scripts/Install-Adventure-Pack.bat" "$STAGE/Install Adventure Pack.bat"
        ;;
esac

cp "$ROOT/scripts/INSTALL.txt" "$STAGE/README.txt"
sed -i.bak "s/@VERSION@/$VERSION/g" "$STAGE/README.txt" && rm -f "$STAGE/README.txt.bak"

cd "$ROOT/dist"
if [ "$PLATFORM" = "macOS" ]; then
    # ditto keeps bundle symlinks, permissions and signatures intact
    ditto -c -k --sequesterRsrc --keepParent "$NAME" "$NAME.zip"
elif command -v zip > /dev/null; then
    zip -qry "$NAME.zip" "$NAME"
else
    powershell -NoProfile -Command "Compress-Archive -Path '$NAME' -DestinationPath '$NAME.zip' -Force"
fi

echo "Packaged $count plugins -> dist/$NAME.zip"
ls -la "$ROOT/dist/$NAME.zip"
