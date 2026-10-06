# adventure_add_plugin(<Target>
#     NAME "Product Name"
#     CODE Abcd                       # unique 4-char plugin code (one upper-case letter first)
#     DESCRIPTION "..."
#     [INSTRUMENT]                    # synth / instrument (MIDI in, audio out)
#     [MIDI_OUT]                      # also emits MIDI
#     VST3_CATEGORIES ...
#     ASSETS font.ttf ...             # embedded into namespace PluginAssets (header PluginAssets.h)
#     SOURCES ...)
#
# Every plugin in the pack shares the same company info, compile settings and the AdventureKit library.

set(AA_COMPANY_NAME "Adventure Audio")
set(AA_MANUFACTURER_CODE "AdvA")

function(adventure_add_plugin target)
    cmake_parse_arguments(AP "INSTRUMENT;MIDI_OUT" "NAME;CODE;DESCRIPTION" "VST3_CATEGORIES;ASSETS;SOURCES" ${ARGN})

    string(TOLOWER "${target}" target_lower)

    if(AP_INSTRUMENT)
        set(is_synth TRUE)
        set(needs_midi_in TRUE)
        set(au_type kAudioUnitType_MusicDevice)
    else()
        set(is_synth FALSE)
        set(needs_midi_in FALSE)
        set(au_type kAudioUnitType_Effect)
    endif()

    if(AP_MIDI_OUT)
        set(needs_midi_out TRUE)
    else()
        set(needs_midi_out FALSE)
    endif()

    juce_add_plugin(${target}
        PRODUCT_NAME "${AP_NAME}"
        COMPANY_NAME "${AA_COMPANY_NAME}"
        COMPANY_WEBSITE "https://github.com/ericrius1/abletonadventures"
        BUNDLE_ID "com.adventureaudio.${target_lower}"
        PLUGIN_MANUFACTURER_CODE ${AA_MANUFACTURER_CODE}
        PLUGIN_CODE ${AP_CODE}
        DESCRIPTION "${AP_DESCRIPTION}"
        FORMATS ${AA_FORMATS}
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT ${needs_midi_in}
        NEEDS_MIDI_OUTPUT ${needs_midi_out}
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE ${au_type}
        VST3_CATEGORIES ${AP_VST3_CATEGORIES}
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(${target} PRIVATE ${AP_SOURCES})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

    if(AP_ASSETS)
        juce_add_binary_data(${target}_Assets
            HEADER_NAME PluginAssets.h
            NAMESPACE PluginAssets
            SOURCES ${AP_ASSETS})
        set_target_properties(${target}_Assets PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "Assets")
        target_link_libraries(${target} PRIVATE ${target}_Assets)
    endif()

    target_compile_definitions(${target}
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_DISPLAY_SPLASH_SCREEN=0
            JUCE_REPORT_APP_USAGE=0
            JUCE_MODAL_LOOPS_PERMITTED=0
            JUCE_USE_XINPUT=0
            JUCE_JACK=0
            JUCE_ALSA=1
            JUCE_PLUGINHOST_LADSPA=0
            JUCE_STRICT_REFCOUNTEDPOINTER=1
            AA_PLUGIN_CODE_STRING="${AP_CODE}")

    target_link_libraries(${target}
        PRIVATE
            aakit
            aa_ui_fonts
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wno-missing-field-initializers>)
        # Let the linker drop unused JUCE code: noticeably smaller plugin binaries.
        target_compile_options(${target} PUBLIC -ffunction-sections -fdata-sections)
        if(APPLE)
            target_link_options(${target} INTERFACE -Wl,-dead_strip)
        else()
            target_link_options(${target} INTERFACE -Wl,--gc-sections)
        endif()
    endif()

    set_target_properties(${target} PROPERTIES FOLDER "Plugins/${target}")
endfunction()
