# example_runner.cmake — compile and link example program(s)
#
# Required variables:
#   COMPILER       path to clumsyc
#   EXAMPLE_DIR    directory containing .cpl source files
#   TMP_DIR        temp directory for intermediate files
#
# Optional variables:
#   LINK_LIBS      extra link libraries (e.g. "-lSDL2")

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

# Compile each .cpl to .o
set(OBJECT_FILES "")
foreach(example_file ${EXAMPLE_FILES})
    get_filename_component(file_name ${example_file} NAME_WE)
    
    execute_process(
        COMMAND ${COMPILER} ${example_file}
        OUTPUT_FILE ${TMP_DIR}/${file_name}.s
        ERROR_VARIABLE COMPILE_ERR
        RESULT_VARIABLE COMPILE_RC
    )
    if(NOT COMPILE_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (compile error in ${file_name}): ${COMPILE_ERR}")
    endif()
    
    # Assemble to .o
    execute_process(
        COMMAND as -o ${TMP_DIR}/${file_name}.o ${TMP_DIR}/${file_name}.s
        ERROR_VARIABLE ASM_ERR
        RESULT_VARIABLE ASM_RC
    )
    if(NOT ASM_RC EQUAL 0)
        message(FATAL_ERROR "FAIL (assembly error in ${file_name}): ${ASM_ERR}")
    endif()
    
    list(APPEND OBJECT_FILES ${TMP_DIR}/${file_name}.o)
endforeach()

# Link
set(LINK_CMD clang -o ${TMP_DIR}/${EXAMPLE_NAME}.x ${OBJECT_FILES})
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

message(STATUS "BUILT: ${EXAMPLE_NAME} -> ${TMP_DIR}/${EXAMPLE_NAME}.x")
