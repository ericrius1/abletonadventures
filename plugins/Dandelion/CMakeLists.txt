adventure_add_plugin(Dandelion
    NAME "Dandelion"
    CODE Dndl
    DESCRIPTION "Granular seed sampler: drop in any sound and blow it into clouds of grains"
    INSTRUMENT
    VST3_CATEGORIES Instrument Sampler
    ASSETS
        "${CMAKE_SOURCE_DIR}/assets/fonts/Sniglet-ExtraBold.ttf"
    SOURCES
        Source/DandelionProcessor.cpp
        Source/DandelionEditor.cpp)
