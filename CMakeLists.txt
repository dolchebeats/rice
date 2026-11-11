
cmake_minimum_required(VERSION 3.22)

project(rice VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE lives inside the repository
add_subdirectory(JUCE)

# Create the plugin
juce_add_plugin(rice
    COMPANY_NAME "dolche"
    BUNDLE_ID "com.dolche.rice"

    PLUGIN_MANUFACTURER_CODE Dolc
    PLUGIN_CODE Rice

    FORMATS VST3 Standalone
    PRODUCT_NAME "rice"
    PLUGIN_NAME "rice"
    DESCRIPTION "Granular delay, reverb and glitch effect"

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE

    VST3_CATEGORIES Fx Delay Reverb

    COPY_PLUGIN_AFTER_BUILD FALSE
)

# Source files
target_sources(rice PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

# JUCE modules
target_link_libraries(rice
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp

    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(rice PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
