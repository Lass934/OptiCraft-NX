if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "StageSwitchData requires SOURCE_ROOT and OUTPUT_ROOT")
endif()

foreach(_tree assets resources)
    if(NOT IS_DIRECTORY "${SOURCE_ROOT}/${_tree}")
        message(FATAL_ERROR
            "Switch runtime data is missing: ${SOURCE_ROOT}/${_tree}\n"
            "Set SWITCH_DATA_ROOT to a directory containing both assets/ and resources/, "
            "then reconfigure the preset.")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT_ROOT}")
file(COPY "${SOURCE_ROOT}/assets" DESTINATION "${OUTPUT_ROOT}")
file(COPY "${SOURCE_ROOT}/resources" DESTINATION "${OUTPUT_ROOT}")
