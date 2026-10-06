#!/usr/bin/env bash
# Developer helper (Linux): build one plugin and run the test-harness checks on it.
#
#   scripts/dev.sh <PluginFolder> build              # configure + build the VST3
#   scripts/dev.sh <PluginFolder> render [args...]   # offline render to WAV (+ level stats)
#   scripts/dev.sh <PluginFolder> stress             # randomised params, block sizes, state round-trip
#   scripts/dev.sh <PluginFolder> gui [args...]      # open the editor with live audio, save a PNG screenshot
#   scripts/dev.sh <PluginFolder> pluginval          # Tracktion pluginval, strictness 7
#   scripts/dev.sh <PluginFolder> all                # all of the above
#
# Environment overrides: AA_BUILD_DIR, AA_JUCE_DIR, AA_HARNESS, AA_OUT, AA_JOBS, DISPLAY
set -euo pipefail

NAME=${1:?plugin folder name, e.g. Stardust}
STEP=${2:-all}
shift 2 || shift $#

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${AA_BUILD_DIR:-/home/user/build-$NAME}
JUCE_DIR=${AA_JUCE_DIR:-/home/user/JUCE}
HARNESS=${AA_HARNESS:-/home/user/build/tools/harness/aa_harness_artefacts/Release/AdventureHarness}
PLUGINVAL=${AA_PLUGINVAL:-/home/user/tools/pluginval}
OUT=${AA_OUT:-/home/user/out/$NAME}
JOBS=${AA_JOBS:-2}
export DISPLAY=${DISPLAY:-:99}

mkdir -p "$OUT"

ensure_display() {
    if ! xdpyinfo -display "$DISPLAY" > /dev/null 2>&1 && ! pgrep -f "Xvfb $DISPLAY" > /dev/null; then
        (Xvfb "$DISPLAY" -screen 0 2400x1600x24 -nolisten tcp > /dev/null 2>&1 &)
        sleep 1
    fi
}

find_vst3() {
    find "$BUILD/plugins/$NAME/${NAME}_artefacts/Release/VST3" -maxdepth 1 -name "*.vst3" -print -quit 2> /dev/null
}

do_build() {
    if [ ! -f "$BUILD/build.ninja" ]; then
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DFETCHCONTENT_SOURCE_DIR_JUCE="$JUCE_DIR" -DAA_ONLY="$NAME" > "$OUT/configure.log" 2>&1 \
            || { tail -30 "$OUT/configure.log"; exit 1; }
    fi
    if ! cmake --build "$BUILD" --target "${NAME}_VST3" -j"$JOBS" > "$OUT/build.log" 2>&1; then
        grep -E "error|Error|FAILED" "$OUT/build.log" | head -60
        echo "BUILD FAILED (full log: $OUT/build.log)"
        exit 1
    fi
    echo "BUILD OK: $(find_vst3)"
}

do_render() {
    local v; v=$(find_vst3)
    "$HARNESS" render "$v" "$OUT/render.wav" --seconds 8 "$@" 2>&1 | grep -v "^$" | tail -20
}

do_stress() {
    local v; v=$(find_vst3)
    "$HARNESS" stress "$v" --seconds 2 2>&1 | tail -20
}

do_gui() {
    ensure_display
    local v; v=$(find_vst3)
    timeout 90 "$HARNESS" gui "$v" "$OUT/screenshot.png" --seconds 5 "$@" 2>&1 | tail -5
}

do_pluginval() {
    ensure_display
    local v; v=$(find_vst3)
    if timeout 900 "$PLUGINVAL" --strictness-level 7 --timeout-ms 120000 --validate "$v" > "$OUT/pluginval.log" 2>&1; then
        echo "PLUGINVAL PASSED"
    else
        grep -B2 -A8 -E "FAILED|!!!" "$OUT/pluginval.log" | head -60
        echo "PLUGINVAL FAILED (full log: $OUT/pluginval.log)"
        return 1
    fi
}

case "$STEP" in
    build) do_build ;;
    render) do_render "$@" ;;
    stress) do_stress ;;
    gui) do_gui "$@" ;;
    pluginval) do_pluginval ;;
    all) do_build; do_render; do_stress; do_gui; do_pluginval ;;
    *) echo "unknown step $STEP"; exit 1 ;;
esac
