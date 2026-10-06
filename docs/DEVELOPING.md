# Developing the Adventure Pack

Everything is C++17 + [JUCE 9](https://juce.com). Each plugin lives in `plugins/<Name>/` and is built
from the shared **AdventureKit** in `kit/aakit/`.

```
CMakeLists.txt              top level: fetches JUCE, adds kit + every plugin folder
cmake/AdventurePlugin.cmake adventure_add_plugin(): one call per plugin (company, codes, formats, fonts)
kit/aakit/                  shared look & feel, widgets, presets, editor/processor bases, DSP
assets/fonts/               embedded OFL/Apache fonts (Poppins for UI + one display face per plugin)
plugins/<Name>/             CMakeLists.txt + Source/
tools/harness/              Linux test host (render / stress / screenshot) - not shipped
scripts/                    dev helper, packaging and install scripts
```

## Building

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release        # add -DAA_ONLY=Boing to build a single plugin
cmake --build build --config Release
```

Artefacts land in `build/plugins/<Name>/<Name>_artefacts/Release/{VST3,AU}/`.
`scripts/package.sh` collects them into the distributable zip.

On Linux there's a one-stop helper that builds a single plugin and runs every check:

```bash
scripts/dev.sh Boing all      # build, render, stress, screenshot, pluginval
```

## Anatomy of a plugin

`plugins/Boing` is the reference implementation - read it first.

**CMakeLists.txt**

```cmake
adventure_add_plugin(Boing
    NAME "Boing"                      # product name shown in Ableton
    CODE Boin                         # unique 4 chars, first upper-case, rest lower-case
    DESCRIPTION "..."
    [INSTRUMENT] [MIDI_OUT]
    VST3_CATEGORIES Fx Delay
    ASSETS "${CMAKE_SOURCE_DIR}/assets/fonts/LuckiestGuy-Regular.ttf"   # -> PluginAssets::LuckiestGuyRegular_ttf
    SOURCES Source/BoingProcessor.cpp Source/BoingEditor.cpp)
```

**Processor** - derive from `aa::PluginBase`:

* Build the parameter layout with the helpers in `aa::params` (`floatParam`, `percent`, `choice`,
  `toggle`, `integer`) - they give every value a nice unit-aware text ("1.2 kHz", "350 ms", "48%").
* Cache `std::atomic<float>*` pointers with `raw("id")` in the constructor; read them in `processBlock`.
* `setFactoryPresets({...})` with values in *real* units; unlisted params reset to their default.
  The first preset should be the default sound (empty value list).
* Non-parameter state (patterns, samples...) goes in `saveExtraState / loadExtraState / resetExtraState`.
* Use `aa::dsp::Transport` for host tempo/position (it free-runs at host tempo when stopped).
* Talk to the UI through atomics or `aa::dsp::SpscQueue` (never locks or allocations on the audio thread).
* End the .cpp with `juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()`.

**Editor** - derive from `aa::EditorBase (processor, baseWidth, baseHeight, theme)`:

* Design at a fixed base size (around 880x560); the window is resizable and everything scales as vectors.
* Call `aa::Fonts::setDisplayTypeface(...)` first, add widgets to `content`, then `finishSetup()` last.
* Override `paintContent` (background, panels), `layoutContent` (base coordinates), `onFrame` (animation).
* Widgets: `aa::Knob`, `aa::Toggle`, `aa::ChoiceBox` (stepper / segmented), `aa::PresetSelector`, `aa::Caption`.
* For a bespoke knob style subclass `aa::LookAndFeel`, override `drawKnob`, and call `useLookAndFeel(...)`
  at the top of the editor constructor.
* Cache expensive static artwork in an `juce::Image` rendered at the physical pixel scale
  (`g.getInternalContext().getPhysicalPixelScaleFactor()`) and only repaint animated regions each frame.

## Ableton notes

* VST3 on Windows and macOS, plus AU on macOS. Live doesn't put VST/AU plugins in its MIDI-effect slot, so
  MIDI-generating devices (like Orrery) are instruments with a built-in sound that *also* output MIDI:
  load it on a MIDI track, then set another track's "MIDI From" to that track.
* Keep the parameter list stable across versions: Live stores automation by parameter index.
* Effects report tails via `tailSeconds`, instruments are stereo-out only.
