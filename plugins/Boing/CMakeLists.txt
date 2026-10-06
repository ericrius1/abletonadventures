adventure_add_plugin(Boing
    NAME "Boing"
    CODE Boin
    DESCRIPTION "Bouncing-ball delay: echoes that fall like a ball bouncing to rest"
    VST3_CATEGORIES Fx Delay
    ASSETS
        "${CMAKE_SOURCE_DIR}/assets/fonts/LuckiestGuy-Regular.ttf"
    SOURCES
        Source/BoingProcessor.cpp
        Source/BoingEditor.cpp)
