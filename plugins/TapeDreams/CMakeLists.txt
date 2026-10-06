adventure_add_plugin(TapeDreams
    NAME "Tape Dreams"
    CODE Tpdr
    DESCRIPTION "Warm, wobbly, nostalgic cassette machine: saturation, wow, flutter, age, hiss and tape stop"
    VST3_CATEGORIES Fx Distortion
    ASSETS
        "${CMAKE_SOURCE_DIR}/assets/fonts/Pacifico-Regular.ttf"
        "${CMAKE_SOURCE_DIR}/assets/fonts/PermanentMarker-Regular.ttf"
    SOURCES
        Source/TapeDreamsProcessor.cpp
        Source/TapeDreamsEditor.cpp
        Source/TapeWidgets.cpp)
