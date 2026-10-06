adventure_add_plugin(CritterKit
    NAME "Critter Kit"
    CODE Crkt
    DESCRIPTION "Creature drum machine: eight synthesized critters, a 16-step sequencer and sample-eating pads"
    INSTRUMENT
    VST3_CATEGORIES Instrument Drum
    ASSETS
        "${CMAKE_SOURCE_DIR}/assets/fonts/LilitaOne-Regular.ttf"
    SOURCES
        Source/CritterProcessor.cpp
        Source/CritterSamples.cpp
        Source/CritterArt.cpp
        Source/CritterEditor.cpp)
