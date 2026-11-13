cmake_minimum_required(VERSION 3.22)

project(NAMplified VERSION 1.0.0 LANGUAGES C CXX)

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

juce_add_plugin(NAMplified
    COMPANY_NAME "NAMplified"
    BUNDLE_ID "org.sedosa.NAMplified"
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Qopw
    PRODUCT_NAME "NAMplified"
    DESCRIPTION "NAMplified guitar signal-chain plugin"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Branding/NAMplifiedAppIcon.png"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS AU VST3 Standalone
)

# macOS needs the audio-input usage text and an ICNS file in the standalone
# bundle. Add both to the finished bundle after linking.
if (APPLE)
    target_compile_definitions(NAMplified_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
    target_sources(NAMplified_Standalone PRIVATE Source/NAMplifiedStandaloneApp.cpp)
    set_property(TARGET NAMplified_Standalone APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Branding/NAMplified.icns"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EnsureMicrophoneUsageDescription.cmake"
    )
    add_custom_command(TARGET NAMplified_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DPLIST_PATH=$<TARGET_BUNDLE_DIR:NAMplified_Standalone>/Contents/Info.plist"
            "-DICON_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/Assets/Branding/NAMplified.icns"
            "-DRESOURCES_DIR=$<TARGET_BUNDLE_DIR:NAMplified_Standalone>/Contents/Resources"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EnsureMicrophoneUsageDescription.cmake"
        COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:NAMplified_Standalone>"
        VERBATIM
    )
endif()

juce_generate_juce_header(NAMplified)

juce_add_binary_data(NAMplifiedAssets
    SOURCES
        Assets/Fonts/InterVariable.ttf
        Assets/Fonts/SpaceGrotesk.ttf
        Assets/Branding/TONE3000Logo.svg
        Assets/Branding/NAMplifiedAppIcon.png
)

target_sources(NAMplified PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Tone3000Client.cpp
    Source/CleanBoost.h
    Source/AlgorithmicReverb.h
    Source/Tone3000Client.h
    Source/Tone3000Config.h
)

target_compile_features(NAMplified PUBLIC cxx_std_17)
target_compile_definitions(NAMplified PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(NAMplified PRIVATE
    NAMplifiedAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_cryptography
    nam_core
    chowdsp_wdf
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Each final wrapper must directly include the NAM Core object files so parser
# registration initializers (notably SlimmableContainer for NAM A2) survive
# static archive dead stripping at the final link step.
foreach(plugin_format AU VST3 Standalone)
    if (TARGET NAMplified_${plugin_format})
        target_link_libraries(NAMplified_${plugin_format} PRIVATE nam_core)
    endif()
endforeach()
