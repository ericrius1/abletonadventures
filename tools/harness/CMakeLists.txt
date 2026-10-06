# Developer test host (not shipped): loads the built VST3s, renders audio offline, stress-tests
# parameters/state, and opens editors with live audio for screenshots.

juce_add_gui_app(aa_harness
    PRODUCT_NAME "AdventureHarness"
    BUNDLE_ID com.adventureaudio.harness
    COMPANY_NAME "Adventure Audio")

target_sources(aa_harness PRIVATE Main.cpp)

target_compile_definitions(aa_harness PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_XINPUT=0
    JUCE_JACK=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:aa_harness,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="1.0.0")

target_link_libraries(aa_harness PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)
