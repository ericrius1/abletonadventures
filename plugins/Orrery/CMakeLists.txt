adventure_add_plugin(Orrery
    NAME "Orrery"
    CODE Orry
    DESCRIPTION "Polyrhythmic planet sequencer with a celestial bell voice and MIDI out"
    INSTRUMENT
    MIDI_OUT
    VST3_CATEGORIES Instrument Synth
    ASSETS
        "${CMAKE_SOURCE_DIR}/assets/fonts/CinzelDecorative-Bold.ttf"
    SOURCES
        Source/OrreryProcessor.cpp
        Source/OrreryLookAndFeel.cpp
        Source/OrreryStage.cpp
        Source/OrreryEditor.cpp)
