# example_runner.cmake — compile and link example program(s)
#
# Required variables:
#   COMPILER       path to clumsyc
#   EXAMPLE_DIR    directory containing .cpl source files
#   TMP_DIR        temp directory for intermediate files
#
# Optional variables:
#   BACKEND        "qbe" (default), "aarch64-legacy", "llvm", or "cranelift"
#   LINK_LIBS      extra link libraries (e.g. "-lSDL2")
#   LLC            path to llc (required for llvm)
#   CLIFC          path to clifc (required for cranelift)

if(NOT DEFINED BACKEND)
    set(BACKEND "qbe")
endif()

# Per-backend output dir so different backends don't clobber each other
set(BACKEND_DIR ${TMP_DIR}/${BACKEND})
file(MAKE_DIRECTORY ${BACKEND_DIR})

file(GLOB EXAMPLE_FILES "${EXAMPLE_DIR}/*.cpl")
list(LENGTH EXAMPLE_FILES FILE_COUNT)
if(FILE_COUNT EQUAL 0)
    message(FATAL_ERROR "No .cpl files found in ${EXAMPLE_DIR}")
endif()

# Sort files so main.cpl comes first if it exists (for naming the binary)
list(SORT EXAMPLE_FILES)
# Move main.cpl to front if present
list(FIND EXAMPLE_FILES "${EXAMPLE_DIR}/main.cpl" MAIN_IDX)
if(NOT MAIN_IDX EQUAL -1)
    list(REMOVE_AT EXAMPLE_FILES ${MAIN_IDX})
    list(INSERT EXAMPLE_FILES 0 "${EXAMPLE_DIR}/main.cpl")
endif()

list(GET EXAMPLE_FILES 0 FIRST_FILE)
get_filename_component(EXAMPLE_NAME ${FIRST_FILE} NAME_WE)

set(OBJECT_FILES "")
foreach(example_file ${EXAMPLE_FILES})
    get_filename_component(file_name ${example_file} NAME_WE)

    if(BACKEND STREQUAL "qbe")
        # QBE pipeline: clumsyc -> QBE IL -> ARM64 asm -> object
        execute_process(
            COMMAND ${COMPILER} --backend=qbe ${example_file}
            OUTPUT_FILE ${BACKEND_DIR}/${file_name}.qbe
            ERROR_VARIABLE COMPILE_ERR
            RESULT_VARIABLE COMPILE_RC
        )
        if(NOT COMPILE_RC EQUAL 0)
            message(FATAL_ERROR "FAIL (compile error in ${file_name}): ${COMPILE_ERR}")
        endif()

        # Variadic externs get x8-zeroing stubs (see stubs.c); link if any
        execute_process(
            COMMAND ${COMPILER} --emit-stubs ${example_file}
            OUTPUT_FILE ${BACKEND_DIR}/${file_name}.stubs.s
            ERROR_QUIET
        )
        if(EXISTS ${BACKEND_DIR}/${file_name}.stubs.s)
            list(APPEND OBJECT_FILES ${BACKEND_DIR}/${file_name}.stubs.s)
        endif()

        execute_process(
            COMMAND qbe -o ${BACKEND_DIR}/${file_name}.s -
            INPUT_FILE ${BACKEND_DIR}/${file_name}.qbe
            ERROR_VARIABLE QBE_ERR
            RESULT_VARIABLE QBE_RC
        )
        if(NOT QBE_RC EQUAL 0)
            message(FATAL_ERROR "FAIL (qbe error in ${file_name}): ${QBE_ERR}")
        endif()

        execute_process(
            COMMAND clang -c -o ${BACKEND_DIR}/${file_name}.o ${BACKEND_DIR}/${file_name}.s
            ERROR_VARIABLE ASM_ERR
            RESULT_VARIABLE ASM_RC
        )
        if(NOT ASM_RC EQUAL 0)
            message(FATAL_ERROR "FAIL (assembly error in ${file_name}): ${ASM_ERR}")
        endif()
    elseif(BACKEND STREQUAL "llvm")
        # LLVM pipeline: clumsyc -> LLVM IR -> ARM64 asm -> object
        execute_process(
            COMMAND ${COMPILER} --backend=llvm ${example_file}
            OUTPUT_FILE ${BACKEND_DIR}/${file_name}.ll
            ERROR_VARIABLE COMPILE_ERR
            RESULT_VARIABLE COMPILE_RC
        )
        if(NOT COMPILE_RC EQUAL 0)
            message(FATAL_ERROR "FAIL (compile error in ${file_name}): ${COMPILE_ERR}")
        endif()

        # Variadic externs get x8-zeroing stubs (see stubs.c); link if any
        execute_process(
            COMMAND ${COMPILER} --emit-stubs ${example_file}
            OUTPUT_FILE ${BACKEND_DIR}/${file_name}.stubs.s
            ERROR_QUIET
        )
        if(EXISTS ${BACKEND_DIR}/${file_name}.stubs.s)
            list(APPEND OBJECT_FILES ${BACKEND_DIR}/${file_name}.stubs.s)
        endif()

        execute_process(
            COMMAND ${LLC} -O1 -o ${BACKEND_DIR}/${file_name}.s ${BACKEND_DIR}/${file_name}.ll
            ERROR_VARIABLE LLC_ERR
            RESULT_VARIABLE LLC_RC
        )
        if(NOT LLC_RC EQUAL 0)
            message(FATAL_ERROR "FAIL (llc error in ${file_name}): ${LLC_ERR}")
        endif()

        execute_process(
            COMMAND clang -c -o ${BACKEND_DIR}/${file_name}.o ${BACKEND_DIR}/${file_name}.s
            ERROR_VARIABLE ASM_ERR
            RESULT_VARIABLE ASM_RC
        )
        if(NOT ASM_RC EQUAL 0)
            message(FATAL_ERROR "FAIL (assembly error in ${file_name}): ${ASM_ERR}")
        endif()
    elseif(BACKEND STREQUAL "cranelift")
        # Cranelift pipeline: clumsyc -> CLIF -> object
        execute_process(
            COMMAND ${COMPILER} --backend=cranelift ${example_file}
            OUTPUT_FILE ${BACKEND_DIR}/${file_name}.clif
            ERROR_VARIABLE COMPILE_ERR
            RESULT_VARIABLE COMPILE_RC
        )
        if(NOT COMPILE_RC EQUAL 0)
            message(FATAL_ERROR "FAIL (compile error in ${file_name}): ${COMPILE_ERR}")
        endif()

        execute_process(
            COMMAND ${CLIFC} ${BACKEND_DIR}/${file_name}.clif ${BACKEND_DIR}/${file_name}.o
            ERROR_VARIABLE CLIFC_ERR
            RESULT_VARIABLE CLIFC_RC
        )
        if(NOT CLIFC_RC EQUAL 0)
            message(FATAL_ERROR "FAIL (clifc error in ${file_name}): ${CLIFC_ERR}")
        endif()

        # Variadic externs get x8-zeroing stubs written next to the object
        set(STUB_FILE ${BACKEND_DIR}/${file_name}.stubs.s)
        if(EXISTS ${STUB_FILE})
            list(APPEND OBJECT_FILES ${STUB_FILE})
        endif()
    else()
        # Legacy ARM64 pipeline: clumsyc -> ARM64 asm -> object
        execute_process(
            COMMAND ${COMPILER} --backend=aarch64-legacy ${example_file}
            OUTPUT_FILE ${BACKEND_DIR}/${file_name}.s
            ERROR_VARIABLE COMPILE_ERR
            RESULT_VARIABLE COMPILE_RC
        )
        if(NOT COMPILE_RC EQUAL 0)
            message(FATAL_ERROR "FAIL (compile error in ${file_name}): ${COMPILE_ERR}")
        endif()

        execute_process(
            COMMAND clang -c -o ${BACKEND_DIR}/${file_name}.o ${BACKEND_DIR}/${file_name}.s
            ERROR_VARIABLE ASM_ERR
            RESULT_VARIABLE ASM_RC
        )
        if(NOT ASM_RC EQUAL 0)
            message(FATAL_ERROR "FAIL (assembly error in ${file_name}): ${ASM_ERR}")
        endif()
    endif()

    list(APPEND OBJECT_FILES ${BACKEND_DIR}/${file_name}.o)
endforeach()

# Link
set(LINK_CMD clang -o ${BACKEND_DIR}/${EXAMPLE_NAME}.x ${OBJECT_FILES})
if(DEFINED LINK_LIBS AND NOT LINK_LIBS STREQUAL "")
    separate_arguments(LIBS_LIST UNIX_COMMAND "${LINK_LIBS}")
    list(APPEND LINK_CMD ${LIBS_LIST})
endif()

execute_process(
    COMMAND ${LINK_CMD}
    ERROR_VARIABLE LINK_ERR
    RESULT_VARIABLE LINK_RC
)
if(NOT LINK_RC EQUAL 0)
    message(FATAL_ERROR "FAIL (link error): ${LINK_ERR}")
endif()

message(STATUS "BUILT: ${EXAMPLE_NAME} (${BACKEND}) -> ${BACKEND_DIR}/${EXAMPLE_NAME}.x")
