
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
    DESCRIPTION "Granular capture and repeat effect"

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE

    VST3_CATEGORIES Fx Delay

    COPY_PLUGIN_AFTER_BUILD FALSE
)

# Source files
target_sources(rice PRIVATE
    src/GrainEngine.cpp
    src/GrainEngine.h
    src/GrainTiming.h
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginEditor.cpp
    src/PluginEditor.h
)

# Offline audio checks
option(RICE_BUILD_TESTS "Build the offline grain engine checks" OFF)
if(RICE_BUILD_TESTS)
    enable_testing()
    add_executable(rice_engine_tests tests/GrainEngineTests.cpp src/GrainEngine.cpp)
    target_include_directories(rice_engine_tests PRIVATE src)
    add_test(NAME rice_engine_tests COMMAND rice_engine_tests)
    add_executable(rice_plugin_tests tests/PluginTests.cpp)
    target_include_directories(rice_plugin_tests PRIVATE src JUCE/modules)
    target_compile_definitions(rice_plugin_tests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    target_link_libraries(rice_plugin_tests PRIVATE rice)
    add_test(NAME rice_plugin_tests COMMAND rice_plugin_tests "${CMAKE_CURRENT_BINARY_DIR}/rice-ui.png")
endif()

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
