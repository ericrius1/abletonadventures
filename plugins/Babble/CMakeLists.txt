adventure_add_plugin(Babble
    NAME "Babble"
    CODE Bbbl
    DESCRIPTION "Singing formant synth with a cartoon face: choirs, robots and gibberish-babbling voices"
    INSTRUMENT
    VST3_CATEGORIES Instrument Synth
    ASSETS
        "${CMAKE_SOURCE_DIR}/assets/fonts/Chewy-Regular.ttf"
    SOURCES
        Source/BabbleEngine.cpp
        Source/BabbleProcessor.cpp
        Source/BabbleWidgets.cpp
        Source/BabbleFace.cpp
        Source/BabbleEditor.cpp)
