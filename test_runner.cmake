# test_runner.cmake — compile, link, run, and compare output
#
# Required variables:
#   COMPILER       path to clumsyc
#   TEST_FILE      path to .cpl test source
#   TMP_DIR        temp directory for intermediate files
#   TEST_DIR       path to tests/ (for .expected files)
#   PRINT_HELPERS  path to print_helpers.o

get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)

# Compile
execute_process(
    COMMAND ${COMPILER} ${TEST_FILE}
    OUTPUT_FILE ${TMP_DIR}/${TEST_NAME}.s
    ERROR_VARIABLE COMPILE_ERR
    RESULT_VARIABLE COMPILE_RC
)
if(NOT COMPILE_RC EQUAL 0)
    message(FATAL_ERROR "FAIL (compile error): ${COMPILE_ERR}")
endif()

# Link
execute_process(
    COMMAND clang -o ${TMP_DIR}/${TEST_NAME}.x ${TMP_DIR}/${TEST_NAME}.s ${PRINT_HELPERS}
    ERROR_VARIABLE LINK_ERR
    RESULT_VARIABLE LINK_RC
)
if(NOT LINK_RC EQUAL 0)
    message(FATAL_ERROR "FAIL (link error): ${LINK_ERR}")
endif()

# Run and compare (only if .expected exists)
set(EXPECTED_FILE ${TEST_DIR}/${TEST_NAME}.expected)
if(EXISTS ${EXPECTED_FILE})
    execute_process(
        COMMAND ${TMP_DIR}/${TEST_NAME}.x
        OUTPUT_FILE ${TMP_DIR}/${TEST_NAME}.actual
        ERROR_QUIET
        RESULT_VARIABLE RUN_RC
    )
    if(NOT RUN_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (runtime error)")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E compare_files ${EXPECTED_FILE} ${TMP_DIR}/${TEST_NAME}.actual
        RESULT_VARIABLE DIFF_RC
    )
    if(NOT DIFF_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (output mismatch)")
    endif()
endif()

message(STATUS "PASS: ${TEST_NAME}")
