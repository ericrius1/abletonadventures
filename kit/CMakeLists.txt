# AdventureKit: the shared look & feel, widgets, preset system and DSP building blocks
# used by every plugin in the pack. It's an INTERFACE library so that its sources are compiled
# into each plugin with that plugin's JUCE configuration.

add_library(aakit INTERFACE)

target_sources(aakit INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/aakit/Fonts.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/aakit/LookAndFeel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/aakit/Widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/aakit/PluginBase.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/aakit/EditorBase.cpp")

target_include_directories(aakit INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

juce_add_binary_data(aa_ui_fonts
    HEADER_NAME AAUiFonts.h
    NAMESPACE AAUiFonts
    SOURCES
        "${CMAKE_SOURCE_DIR}/assets/fonts/Poppins-Medium.ttf"
        "${CMAKE_SOURCE_DIR}/assets/fonts/Poppins-Bold.ttf")

set_target_properties(aa_ui_fonts PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "Assets")
