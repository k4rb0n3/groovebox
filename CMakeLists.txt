cmake_minimum_required(VERSION 3.22)
project(Groovebox VERSION 0.7.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # Static runtime: the plugin works without installing the Visual C++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Groovebox
    COMPANY_NAME "PJH"
    BUNDLE_ID com.pjh.groovebox
    PLUGIN_MANUFACTURER_CODE Pjhm
    PLUGIN_CODE Pgbx
    PRODUCT_NAME "Groovebox"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum
    FORMATS VST3 Standalone)

target_sources(Groovebox PRIVATE
    Source/Generator.cpp
    Source/SampleAnalysis.cpp
    Source/GrooveAnalysis.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Groovebox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Groovebox
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional headless test harness (not needed to build the plugin).
option(CG_BUILD_TESTS "Build the Groovebox test harness" OFF)
if(CG_BUILD_TESTS)
    juce_add_console_app(GrooveboxTest PRODUCT_NAME "GrooveboxTest")
    target_sources(GrooveboxTest PRIVATE Tests/TestMain.cpp Source/Generator.cpp Source/SampleAnalysis.cpp Source/GrooveAnalysis.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(GrooveboxTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(GrooveboxTest PRIVATE juce::juce_audio_utils juce::juce_audio_formats
        juce::juce_recommended_config_flags)
endif()
