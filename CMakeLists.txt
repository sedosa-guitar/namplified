cmake_minimum_required(VERSION 3.22)

project(NAMinator VERSION 1.0.0 LANGUAGES C CXX)

include(FetchContent)

# Use an installed JUCE checkout when provided; otherwise fetch the version
# used to generate the existing Projucer project.
set(JUCE_SOURCE_DIR "" CACHE PATH "Path to a JUCE source checkout")
if (JUCE_SOURCE_DIR)
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

FetchContent_Declare(
    NAMCore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG v0.5.4
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_GetProperties(NAMCore)
if (NOT namcore_POPULATED)
    FetchContent_Populate(NAMCore)
endif()
file(GLOB_RECURSE NAM_CORE_SOURCES CONFIGURE_DEPENDS "${namcore_SOURCE_DIR}/NAM/*.cpp")
# NAM Core registers model architecture parsers through static initializers.
# Keep its translation units in the final plugin instead of an archive: the
# linker can otherwise omit unreferenced parser objects such as container.cpp,
# leaving A2 SlimmableContainer captures unsupported at runtime.
add_library(nam_core OBJECT ${NAM_CORE_SOURCES})
target_include_directories(nam_core PUBLIC
    "${namcore_SOURCE_DIR}"
    "${namcore_SOURCE_DIR}/NAM"
    "${namcore_SOURCE_DIR}/Dependencies/eigen"
    "${namcore_SOURCE_DIR}/Dependencies/nlohmann"
)
target_compile_features(nam_core PUBLIC cxx_std_20)
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT=1 NAM_ENABLE_A2_FAST=1)

# Header-only Wave Digital Filter components used by the Boost circuit model.
set(CHOWDSP_WDF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CHOWDSP_WDF_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    chowdsp_wdf
    GIT_REPOSITORY https://github.com/Chowdhury-DSP/chowdsp_wdf.git
    GIT_TAG v1.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(chowdsp_wdf)

juce_add_plugin(NAMinator
    COMPANY_NAME "NAMinator"
    BUNDLE_ID "org.sedosa.NAMinator"
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Qopw
    PRODUCT_NAME "NAMinator"
    DESCRIPTION "NAMinator guitar signal-chain plugin"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Branding/NAMinatorAppIcon.png"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS AU VST3 Standalone
)

# macOS requires a usage description before a standalone app can access audio
# input devices. JUCE's generated standalone plist does not currently include
# this key, so add it to the built bundle after linking.
if (APPLE)
    target_compile_definitions(NAMinator_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
    target_sources(NAMinator_Standalone PRIVATE Source/NAMinatorStandaloneApp.cpp)
    add_custom_command(TARGET NAMinator_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DPLIST_PATH=$<TARGET_BUNDLE_DIR:NAMinator_Standalone>/Contents/Info.plist"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EnsureMicrophoneUsageDescription.cmake"
        VERBATIM
    )
endif()

juce_generate_juce_header(NAMinator)

juce_add_binary_data(NAMinatorAssets
    SOURCES
        Assets/Fonts/InterVariable.ttf
        Assets/Fonts/SpaceGrotesk.ttf
        Assets/Branding/TONE3000Logo.svg
)

target_sources(NAMinator PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Tone3000Client.cpp
    Source/CleanBoost.h
    Source/AlgorithmicReverb.h
    Source/AmpSimBridge.h
    Source/Tone3000Client.h
    Source/Tone3000Config.h
)

# The amp-only DSP is derived from 0xFX 1.3.1 (MIT). Keep its license in
# ThirdParty/0xFX and compile only the amp and shared biquad implementation;
# NAMinator continues to use its own UI, routing, pedals, and cabinet stages.
add_library(naminator_amp_sim STATIC
    Source/AmpSimBridge.c
    ThirdParty/0xFX/engine/internal/amp.c
    ThirdParty/0xFX/engine/internal/biquad.c
)
target_include_directories(naminator_amp_sim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/0xFX"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/0xFX/deps"
)
target_compile_features(naminator_amp_sim PRIVATE c_std_99)
if (UNIX)
    target_link_libraries(naminator_amp_sim PRIVATE m)
endif()
set_target_properties(naminator_amp_sim PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_features(NAMinator PUBLIC cxx_std_17)
target_compile_definitions(NAMinator PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(NAMinator PRIVATE
    NAMinatorAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_cryptography
    nam_core
    naminator_amp_sim
    chowdsp_wdf
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Each final wrapper must directly include the NAM Core object files so parser
# registration initializers (notably SlimmableContainer for NAM A2) survive
# static archive dead stripping at the final link step.
foreach(plugin_format AU VST3 Standalone)
    if (TARGET NAMinator_${plugin_format})
        target_link_libraries(NAMinator_${plugin_format} PRIVATE nam_core)
    endif()
endforeach()
