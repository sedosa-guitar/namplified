if (NOT DEFINED PLIST_PATH)
    message(FATAL_ERROR "PLIST_PATH is required")
endif()
if (NOT DEFINED ICON_SOURCE OR NOT EXISTS "${ICON_SOURCE}")
    message(FATAL_ERROR "ICON_SOURCE must point to the NAMplified ICNS asset")
endif()
if (NOT DEFINED RESOURCES_DIR)
    message(FATAL_ERROR "RESOURCES_DIR is required")
endif()

file(MAKE_DIRECTORY "${RESOURCES_DIR}")
file(COPY_FILE "${ICON_SOURCE}" "${RESOURCES_DIR}/NAMplified.icns" ONLY_IF_DIFFERENT)
# JUCE also bundles its own AppIcon.icns from ICON_BIG. Keep both resources
# identical so Finder, LaunchServices and the Dock cannot pick different art.
file(COPY_FILE "${ICON_SOURCE}" "${RESOURCES_DIR}/AppIcon.icns" ONLY_IF_DIFFERENT)

set(description "NAMplified uses audio input to process your instrument signal.")
execute_process(
    COMMAND /usr/libexec/PlistBuddy -c "Set :NSMicrophoneUsageDescription ${description}" "${PLIST_PATH}"
    RESULT_VARIABLE set_result
    OUTPUT_QUIET
    ERROR_QUIET
)

if (NOT set_result EQUAL 0)
    execute_process(
        COMMAND /usr/libexec/PlistBuddy -c "Add :NSMicrophoneUsageDescription string ${description}" "${PLIST_PATH}"
        RESULT_VARIABLE add_result
        OUTPUT_QUIET
        ERROR_VARIABLE add_error
    )
    if (NOT add_result EQUAL 0)
        message(FATAL_ERROR "Could not add microphone usage description: ${add_error}")
    endif()
endif()

execute_process(
    COMMAND /usr/libexec/PlistBuddy -c "Set :CFBundleIconFile NAMplified.icns" "${PLIST_PATH}"
    RESULT_VARIABLE icon_set_result
    OUTPUT_QUIET
    ERROR_QUIET
)

if (NOT icon_set_result EQUAL 0)
    execute_process(
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string NAMplified.icns" "${PLIST_PATH}"
        RESULT_VARIABLE icon_add_result
        OUTPUT_QUIET
        ERROR_VARIABLE icon_add_error
    )
    if (NOT icon_add_result EQUAL 0)
        message(FATAL_ERROR "Could not set the app icon in the standalone plist: ${icon_add_error}")
    endif()
endif()
