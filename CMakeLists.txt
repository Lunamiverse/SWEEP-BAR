# Standard JUCE CMake build: macOS (AU + VST3), Windows (VST3), Linux (VST3).
#   git clone --depth 1 --branch 7.0.12 https://github.com/juce-framework/JUCE.git
#   cmake -B build -DJUCE_DIR=JUCE -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
# GitHub Actions builds this on every push (.github/workflows/build.yml).
cmake_minimum_required (VERSION 3.22)
project (Sweepbar VERSION 0.6.0)

set (JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE 7")
add_subdirectory (${JUCE_DIR} JUCE)

set (SWEEPBAR_FORMATS VST3)
if (APPLE)
    list (APPEND SWEEPBAR_FORMATS AU)
endif()

juce_add_plugin (Sweepbar
    COMPANY_NAME "Sweepbar"
    PLUGIN_MANUFACTURER_CODE Swbp
    PLUGIN_CODE Swb1
    FORMATS ${SWEEPBAR_FORMATS}
    BUNDLE_ID com.sweepbar.sweepbar
    AU_MAIN_TYPE kAudioUnitType_Effect
    PRODUCT_NAME "Sweepbar"
    DESCRIPTION "Beat-synced vocal ear candy"
    VST3_CATEGORIES Fx Modulation
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources (Sweepbar PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Skin.cpp
    Source/FontData.cpp)

target_include_directories (Sweepbar PRIVATE Source)

target_compile_definitions (Sweepbar PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries (Sweepbar
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
