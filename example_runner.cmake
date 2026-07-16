# example_runner.cmake — compile and link an example program
#
# Required variables:
#   COMPILER       path to clumsyc
#   EXAMPLE_FILE   path to .cpl example source
#   TMP_DIR        temp directory for intermediate files
#   PRINT_HELPERS  path to print_helpers.o
#
# Optional variables:
#   LINK_LIBS      extra link libraries (e.g. "-lSDL2")

get_filename_component(EXAMPLE_NAME ${EXAMPLE_FILE} NAME_WE)

# Compile
execute_process(
    COMMAND ${COMPILER} ${EXAMPLE_FILE}
    OUTPUT_FILE ${TMP_DIR}/${EXAMPLE_NAME}.s
    ERROR_VARIABLE COMPILE_ERR
    RESULT_VARIABLE COMPILE_RC
)
if(NOT COMPILE_RC EQUAL 0)
    message(FATAL_ERROR "FAIL (compile error): ${COMPILE_ERR}")
endif()

# Link
set(LINK_CMD clang -o ${TMP_DIR}/${EXAMPLE_NAME}.x ${TMP_DIR}/${EXAMPLE_NAME}.s ${PRINT_HELPERS})
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
