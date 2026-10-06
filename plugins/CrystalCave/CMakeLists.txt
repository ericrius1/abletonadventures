adventure_add_plugin(CrystalCave
    NAME "Crystal Cave"
    CODE Crcv
    DESCRIPTION "Shimmering crystalline reverb: a glowing cave of pitch-climbing echoes"
    VST3_CATEGORIES Fx Reverb
    ASSETS
        "${CMAKE_SOURCE_DIR}/assets/fonts/JuliusSansOne-Regular.ttf"
    SOURCES
        Source/CrystalProcessor.cpp
        Source/CrystalEditor.cpp
        Source/CrystalLookAndFeel.cpp
        Source/CaveArt.cpp)
