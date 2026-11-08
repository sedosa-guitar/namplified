if (NOT DEFINED PLIST_PATH)
    message(FATAL_ERROR "PLIST_PATH is required")
endif()

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
