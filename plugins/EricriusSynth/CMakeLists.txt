adventure_add_plugin(EricriusSynth
    NAME "ericriusSynth"
    CODE ErSy
    DESCRIPTION "Wavetable synthesizer: three morphing wavetable oscillators with warp modes, 16-voice unison, dual filters, drag-and-drop modulation and a ten-effect rack"
    INSTRUMENT
    VST3_CATEGORIES Instrument Synth
    ASSETS
        "${CMAKE_SOURCE_DIR}/assets/fonts/Audiowide-Regular.ttf"
    SOURCES
        Source/Wavetable.cpp
        Source/WavetableFactory.cpp
        Source/Filters.cpp
        Source/Fx.cpp
        Source/Params.cpp
        Source/Modulation.cpp
        Source/Engine.cpp
        Source/SynthProcessor.cpp
        Source/Presets.cpp
        Source/SynthEditor.cpp
        Source/Views.cpp
        Source/ModEditors.cpp
        Source/Pages.cpp)
