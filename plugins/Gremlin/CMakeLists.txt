adventure_add_plugin(Gremlin
    NAME "Gremlin"
    CODE Grml
    DESCRIPTION "Mischievous tempo-synced glitch machine: stutters, reverses, tape stops and bit-crushes on the grid"
    VST3_CATEGORIES Fx
    ASSETS
        "${CMAKE_SOURCE_DIR}/assets/fonts/RubikGlitch-Regular.ttf"
        "${CMAKE_SOURCE_DIR}/assets/fonts/VT323-Regular.ttf"
    SOURCES
        Source/GremlinEngine.cpp
        Source/GremlinProcessor.cpp
        Source/GremlinFace.cpp
        Source/GremlinWidgets.cpp
        Source/GremlinEditor.cpp)
