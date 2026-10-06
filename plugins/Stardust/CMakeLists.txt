adventure_add_plugin(Stardust
    NAME "Stardust"
    CODE Stdu
    DESCRIPTION "Cosmic supersaw pad synth with twinkling star glints, analog drift and a vast space reverb"
    INSTRUMENT
    VST3_CATEGORIES Instrument Synth
    ASSETS
        "${CMAKE_SOURCE_DIR}/assets/fonts/Audiowide-Regular.ttf"
    SOURCES
        Source/StardustProcessor.cpp
        Source/StardustEngine.cpp
        Source/StardustEditor.cpp
        Source/SpaceView.cpp)
