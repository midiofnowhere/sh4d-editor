cmake_minimum_required(VERSION 3.22)
project(SH4dEditor VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SH4D_BUILD_TESTS "Build the headless MIDI logic tests" OFF)
set(SH4D_JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout (otherwise it is fetched)")

if(SH4D_JUCE_PATH)
    add_subdirectory(${SH4D_JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(SH4D_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SH4D_FORMATS AU)
endif()

juce_add_plugin(SH4dEditor
    COMPANY_NAME "DIY"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Diyy
    PLUGIN_CODE Sh4e
    FORMATS ${SH4D_FORMATS}
    PRODUCT_NAME "SH-4d Editor")

target_sources(SH4dEditor PRIVATE
    Source/CcTable.h
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp)

target_compile_definitions(SH4dEditor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SH4dEditor
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(SH4D_BUILD_TESTS)
    juce_add_console_app(SH4dTests PRODUCT_NAME "SH4dTests")
    target_sources(SH4dTests PRIVATE
        Tests/ProcessorTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(SH4dTests PRIVATE Source)
    target_compile_definitions(SH4dTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_UNIT_TESTS=0)
    target_link_libraries(SH4dTests PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
