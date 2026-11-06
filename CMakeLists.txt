cmake_minimum_required(VERSION 3.22)

project(MASTEAL VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE 8 via FetchContent (nessuna installazione globale) -----------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Plugin VST3 --------------------------------------------------------------
juce_add_plugin(MASTEAL
    COMPANY_NAME              "Matteo Petri"
    BUNDLE_ID                 "com.matteopetri.masteal"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   OFF
    PLUGIN_MANUFACTURER_CODE  Mstl
    PLUGIN_CODE               Mstl
    FORMATS                   VST3
    PRODUCT_NAME              "MASTEAL")

target_sources(MASTEAL PRIVATE
    Source/Processor.cpp
    Source/Editor.cpp
    Source/reference/RefTransport.cpp
    Source/analysis/LoudnessMeter.cpp
    Source/ui/WaveformComponent.cpp)

target_compile_definitions(MASTEAL PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MASTEAL PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# --- Test (logica pura) --------------------------------------------------------
juce_add_console_app(MASTEAL_Tests)

target_sources(MASTEAL_Tests PRIVATE
    Tests/Main.cpp
    Tests/TestSharedState.cpp
    Tests/TestRefTransport.cpp
    Tests/TestLoudness.cpp
    Source/Processor.cpp
    Source/Editor.cpp
    Source/reference/RefTransport.cpp
    Source/analysis/LoudnessMeter.cpp
    Source/ui/WaveformComponent.cpp)

target_link_libraries(MASTEAL_Tests PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)

enable_testing()
add_test(NAME MASTEAL_Tests COMMAND $<TARGET_FILE:MASTEAL_Tests>)
