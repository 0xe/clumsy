# Test runner script for CTest
# This script replicates the complex test logic from the Makefile

# Extract test name from file
get_filename_component(TEST_NAME_ONLY ${TEST_FILE} NAME_WE)

# Run the compiler
execute_process(
        COMMAND ${COMPILER} ${TEST_FILE}
        OUTPUT_FILE ${TMP_DIR}/${TEST_NAME_ONLY}.s
        ERROR_QUIET
        RESULT_VARIABLE COMPILE_RESULT
)

if(NOT COMPILE_RESULT EQUAL 0)
    message(FATAL_ERROR "FAIL (compile error)")
endif()

# Try to link with clang
execute_process(
        COMMAND clang -o ${TMP_DIR}/${TEST_NAME_ONLY}.x
        ${TMP_DIR}/${TEST_NAME_ONLY}.s
        ${SRC_DIR}/print_helpers.o
        ERROR_QUIET
        RESULT_VARIABLE LINK_RESULT
)

if(NOT LINK_RESULT EQUAL 0)
    message(FATAL_ERROR "FAIL (link error)")
endif()

# Check if expected output file exists
set(EXPECTED_FILE ${TEST_DIR}/${TEST_NAME_ONLY}.expected)
if(EXISTS ${EXPECTED_FILE})
    # Run the executable and capture output
    execute_process(
            COMMAND ${TMP_DIR}/${TEST_NAME_ONLY}.x
            OUTPUT_FILE ${TMP_DIR}/${TEST_NAME_ONLY}.actual
            ERROR_QUIET
            RESULT_VARIABLE RUN_RESULT
    )

    if(NOT RUN_RESULT EQUAL 0)
        message(FATAL_ERROR "FAIL (runtime error)")
    endif()

    # Compare expected vs actual output
    execute_process(
            COMMAND ${CMAKE_COMMAND} -E compare_files
            ${EXPECTED_FILE} ${TMP_DIR}/${TEST_NAME_ONLY}.actual
            RESULT_VARIABLE DIFF_RESULT
    )

    if(NOT DIFF_RESULT EQUAL 0)
        message(FATAL_ERROR "FAIL (output mismatch)")
    endif()
endif()

message(STATUS "PASS: ${TEST_NAME_ONLY}")