# test_runner.cmake — compile, link, run, and compare output
#
# Required variables:
#   COMPILER       path to clumsyc
#   TEST_FILE      path to .cpl test source
#   TMP_DIR        temp directory for intermediate files
#   TEST_DIR       path to tests/ (for .expected files)
#
# Optional:
#   BACKEND        "qbe" (default), "legacy", "llvm", or "cranelift"
#   CLIFC          path to tools/clifc (required for cranelift)

if(NOT DEFINED BACKEND)
    set(BACKEND "qbe")
endif()

get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)

if(BACKEND STREQUAL "qbe")
    # QBE pipeline: source → QBE IL → assembly → object → executable
    execute_process(
        COMMAND ${COMPILER} --backend=qbe ${TEST_FILE}
        OUTPUT_FILE ${TMP_DIR}/${TEST_NAME}.qbe
        ERROR_VARIABLE COMPILE_ERR
        RESULT_VARIABLE COMPILE_RC
    )
    if(NOT COMPILE_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (compile error): ${COMPILE_ERR}")
    endif()

    # Variadic externs get x8-zeroing stubs (see stubs.c); link if any
    execute_process(
        COMMAND ${COMPILER} --emit-stubs ${TEST_FILE}
        OUTPUT_FILE ${TMP_DIR}/${TEST_NAME}.stubs.s
        ERROR_QUIET
    )
    set(STUB_FILE ${TMP_DIR}/${TEST_NAME}.stubs.s)

    execute_process(
        COMMAND qbe -o ${TMP_DIR}/${TEST_NAME}.s -
        INPUT_FILE ${TMP_DIR}/${TEST_NAME}.qbe
        ERROR_VARIABLE QBE_ERR
        RESULT_VARIABLE QBE_RC
    )
    if(NOT QBE_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (qbe error): ${QBE_ERR}")
    endif()

    execute_process(
        COMMAND clang -c -o ${TMP_DIR}/${TEST_NAME}.o ${TMP_DIR}/${TEST_NAME}.s
        ERROR_VARIABLE ASM_ERR
        RESULT_VARIABLE ASM_RC
    )
    if(NOT ASM_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (assemble error): ${ASM_ERR}")
    endif()

    set(LINK_OBJS ${TMP_DIR}/${TEST_NAME}.o)
    if(EXISTS ${STUB_FILE})
        list(APPEND LINK_OBJS ${STUB_FILE})
    endif()

    execute_process(
        COMMAND clang -o ${TMP_DIR}/${TEST_NAME}.x ${LINK_OBJS}
        ERROR_VARIABLE LINK_ERR
        RESULT_VARIABLE LINK_RC
    )
    if(NOT LINK_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (link error): ${LINK_ERR}")
    endif()
elseif(BACKEND STREQUAL "llvm")
    # LLVM pipeline: source → LLVM IR → assembly → object → executable
    execute_process(
        COMMAND ${COMPILER} --backend=llvm ${TEST_FILE}
        OUTPUT_FILE ${TMP_DIR}/${TEST_NAME}.ll
        ERROR_VARIABLE COMPILE_ERR
        RESULT_VARIABLE COMPILE_RC
    )
    if(NOT COMPILE_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (compile error): ${COMPILE_ERR}")
    endif()

    # Variadic externs get x8-zeroing stubs (see stubs.c); link if any
    execute_process(
        COMMAND ${COMPILER} --emit-stubs ${TEST_FILE}
        OUTPUT_FILE ${TMP_DIR}/${TEST_NAME}.stubs.s
        ERROR_QUIET
    )
    set(STUB_FILE ${TMP_DIR}/${TEST_NAME}.stubs.s)

    execute_process(
        COMMAND ${LLC} -O1 -o ${TMP_DIR}/${TEST_NAME}.s ${TMP_DIR}/${TEST_NAME}.ll
        ERROR_VARIABLE LLC_ERR
        RESULT_VARIABLE LLC_RC
    )
    if(NOT LLC_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (llc error): ${LLC_ERR}")
    endif()

    execute_process(
        COMMAND clang -c -o ${TMP_DIR}/${TEST_NAME}.o ${TMP_DIR}/${TEST_NAME}.s
        ERROR_VARIABLE ASM_ERR
        RESULT_VARIABLE ASM_RC
    )
    if(NOT ASM_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (assemble error): ${ASM_ERR}")
    endif()

    set(LINK_OBJS ${TMP_DIR}/${TEST_NAME}.o)
    if(EXISTS ${STUB_FILE})
        list(APPEND LINK_OBJS ${STUB_FILE})
    endif()

    execute_process(
        COMMAND clang -o ${TMP_DIR}/${TEST_NAME}.x ${LINK_OBJS}
        ERROR_VARIABLE LINK_ERR
        RESULT_VARIABLE LINK_RC
    )
    if(NOT LINK_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (link error): ${LINK_ERR}")
    endif()
elseif(BACKEND STREQUAL "cranelift")
    # Cranelift pipeline: source → CLIF → object → executable
    execute_process(
        COMMAND ${COMPILER} --backend=cranelift ${TEST_FILE}
        OUTPUT_FILE ${TMP_DIR}/${TEST_NAME}.clif
        ERROR_VARIABLE COMPILE_ERR
        RESULT_VARIABLE COMPILE_RC
    )
    if(NOT COMPILE_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (compile error): ${COMPILE_ERR}")
    endif()

    execute_process(
        COMMAND ${CLIFC} ${TMP_DIR}/${TEST_NAME}.clif ${TMP_DIR}/${TEST_NAME}.o
        ERROR_VARIABLE CLIFC_ERR
        RESULT_VARIABLE CLIFC_RC
    )
    if(NOT CLIFC_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (clifc error): ${CLIFC_ERR}")
    endif()

    # Variadic externs get x8-zeroing stubs written next to the object
    set(STUB_FILE ${TMP_DIR}/${TEST_NAME}.stubs.s)
    set(LINK_OBJS ${TMP_DIR}/${TEST_NAME}.o)
    if(EXISTS ${STUB_FILE})
        list(APPEND LINK_OBJS ${STUB_FILE})
    endif()

    execute_process(
        COMMAND clang -o ${TMP_DIR}/${TEST_NAME}.x ${LINK_OBJS}
        ERROR_VARIABLE LINK_ERR
        RESULT_VARIABLE LINK_RC
    )
    if(NOT LINK_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (link error): ${LINK_ERR}")
    endif()
else()
    # Legacy ARM64 pipeline: source → assembly → executable
    execute_process(
        COMMAND ${COMPILER} --backend=aarch64-legacy ${TEST_FILE}
        OUTPUT_FILE ${TMP_DIR}/${TEST_NAME}.s
        ERROR_VARIABLE COMPILE_ERR
        RESULT_VARIABLE COMPILE_RC
    )
    if(NOT COMPILE_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (compile error): ${COMPILE_ERR}")
    endif()

    execute_process(
        COMMAND clang -o ${TMP_DIR}/${TEST_NAME}.x ${TMP_DIR}/${TEST_NAME}.s
        ERROR_VARIABLE LINK_ERR
        RESULT_VARIABLE LINK_RC
    )
    if(NOT LINK_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (link error): ${LINK_ERR}")
    endif()
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
