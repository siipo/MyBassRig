cmake_minimum_required(VERSION 3.22)

project(BassRig VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS YES)

option(BASSRIG_BUILD_TESTS "Build the unit tests" ON)
option(BASSRIG_NAIVE_SHAPERS "Diagnostic: bypass ADAA, waveshape naively" OFF)

include(FetchContent)

# JUCE 8. Pinned rather than tracking a branch so builds are reproducible.
# JUCE 9 is out but chowdsp_utils (needed from step 4 onward, see DESIGN.md)
# is proven against 8.x -- upgrade once that's verified, not before.
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# ADAA (antiderivative anti-aliased) waveshapers. GPLv3, which is fine given
# BassRig's own licence -- see DESIGN.md section 6.
FetchContent_Declare(chowdsp_utils
    GIT_REPOSITORY https://github.com/Chowdhury-DSP/chowdsp_utils.git
    GIT_TAG v2.4.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(chowdsp_utils)

# Wave digital filters, for the Baxandall tone stack. BSD-3, header only.
#
# SOURCE_SUBDIR points at a directory that does not exist on purpose: that makes
# FetchContent download the sources without running add_subdirectory on them.
# chowdsp_wdf v1.0.0 declares cmake_minimum_required below 3.5, which CMake 4
# refuses outright, and since the library is header only its build system buys
# us nothing. Cleaner than forcing CMAKE_POLICY_VERSION_MINIMUM globally.
FetchContent_Declare(chowdsp_wdf
    GIT_REPOSITORY https://github.com/Chowdhury-DSP/chowdsp_wdf.git
    GIT_TAG v1.0.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR do-not-configure)
FetchContent_MakeAvailable(chowdsp_wdf)

# ---------------------------------------------------------------------------
# DSP, built as a plain static lib so the tests can link it without dragging
# in the whole plugin wrapper.
# ---------------------------------------------------------------------------
add_library(BassRigDSP STATIC
    Source/dsp/CompressorPedal/CompressorPedal.cpp
    Source/dsp/ChorusPedal/ChorusPedal.cpp
    Source/dsp/NoiseGatePedal/NoiseGatePedal.cpp
    Source/dsp/OctaverPedal/OctaverPedal.cpp
    Source/dsp/EnvelopeFilterPedal/EnvelopeFilterPedal.cpp
    Source/dsp/PhaserPedal/PhaserPedal.cpp
    Source/dsp/DrivePedal/DrivePedal.cpp
    Source/Presets.cpp
    Source/dsp/ToneStack.cpp
    Source/dsp/PedalRegistry.cpp
    Source/params/Parameters.cpp
)

# The VST3 is a shared module, and on Linux you cannot link non-PIC objects into
# one: ld rejects the R_X86_64_PC32 relocations outright. Windows and macOS do
# not care, so this only ever shows up on a Linux build.
set_target_properties(BassRigDSP PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_include_directories(BassRigDSP PUBLIC Source ${chowdsp_wdf_SOURCE_DIR}/include)

target_link_libraries(BassRigDSP
    PUBLIC
        juce::juce_dsp
        juce::juce_audio_processors
        juce::juce_audio_basics
        chowdsp::chowdsp_waveshapers
        chowdsp::chowdsp_filters
        chowdsp::chowdsp_presets
        chowdsp::chowdsp_compressor
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(BassRigDSP PUBLIC
    BASSRIG_NAIVE_SHAPERS=$<BOOL:${BASSRIG_NAIVE_SHAPERS}>
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=1  # required by the JUCE free/AGPL licence
)

# ---------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------
# AU only builds on Apple platforms, and juce_add_plugin errors rather than
# skipping it if it is listed elsewhere.
set(BASSRIG_FORMATS VST3 Standalone)

if(APPLE)
    list(APPEND BASSRIG_FORMATS AU)
endif()

juce_add_plugin(BassRig
    PRODUCT_NAME "BassRig"
    COMPANY_NAME "BassRig"
    PLUGIN_MANUFACTURER_CODE Brig
    PLUGIN_CODE Bsrg
    FORMATS ${BASSRIG_FORMATS}
    VST3_CATEGORIES Fx Distortion
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(BASSRIG_UI_SOURCES
    Source/PluginEditor.cpp
    Source/ui/PedalLookAndFeel.cpp
    Source/ui/PedalControls.cpp
    Source/ui/PresetBar.cpp
    Source/ui/GainReductionMeter.cpp
    Source/ui/RackView.cpp
    Source/ui/ChainStrip.cpp)

target_sources(BassRig PRIVATE
    Source/PluginProcessor.cpp
    ${BASSRIG_UI_SOURCES})

target_link_libraries(BassRig
    PRIVATE
        BassRigDSP
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
# Renders the editor to PNG headlessly, so the pedal face can be inspected
# without loading the plugin into a host.
juce_add_console_app(BassRigSnapshot)

target_sources(BassRigSnapshot PRIVATE
    tools/Snapshot.cpp
    Source/PluginProcessor.cpp
    ${BASSRIG_UI_SOURCES})

target_include_directories(BassRigSnapshot PRIVATE Source)

target_link_libraries(BassRigSnapshot
    PRIVATE
        BassRigDSP
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_recommended_config_flags)

target_compile_definitions(BassRigSnapshot PRIVATE
    JucePlugin_Name="BassRig"
    JUCE_STANDALONE_APPLICATION=1)

if(BASSRIG_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    juce_add_console_app(BassRigTests)

    target_sources(BassRigTests PRIVATE
        tests/BlendLatencyTests.cpp
        tests/ParameterTests.cpp
        tests/DriveTests.cpp
        tests/ToneStackTests.cpp
        tests/VoicingTests.cpp
        tests/CompressorTests.cpp
        tests/EnvelopeFilterTests.cpp
        tests/ModulationTests.cpp
        tests/ChainOrderTests.cpp
        tests/OctaverGateTests.cpp
        tests/EditorTests.cpp
        tests/PresetTests.cpp
        Source/PluginProcessor.cpp
        ${BASSRIG_UI_SOURCES})

    target_link_libraries(BassRigTests
        PRIVATE
            BassRigDSP
            Catch2::Catch2WithMain
            juce::juce_audio_utils
            juce::juce_gui_basics
            juce::juce_recommended_config_flags)

    target_compile_definitions(BassRigTests PRIVATE
        JucePlugin_Name="BassRig"
        JUCE_STANDALONE_APPLICATION=1)

    enable_testing()
    include(Catch)
    catch_discover_tests(BassRigTests)
endif()
