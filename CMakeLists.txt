cmake_minimum_required(VERSION 3.16)
project(clumsyc VERSION 1.0.0 LANGUAGES C)

# Set C standard and compiler flags
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Set build type to Release with debug info if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

# Source files
set(SOURCES
        src/main.c
        src/tokenizer.c
        src/parser.c
        src/compiler.c
        src/print_helpers.c
)

# Header files (for IDE organization)
set(HEADERS
        src/types.h
        src/tokenizer.h
        src/parser.h
        src/compiler.h
)

# Create the main executable
add_executable(clumsyc ${SOURCES} ${HEADERS})

# Set output directory
set_target_properties(clumsyc PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Install target
install(TARGETS clumsyc DESTINATION bin)

# Enable testing
enable_testing()

# Find test files
file(GLOB TEST_FILES "${CMAKE_SOURCE_DIR}/tests/*.cpl")

# Create temporary directory for tests
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tmp)

# Custom target for running tests
add_custom_target(test-compiler
        COMMAND ${CMAKE_COMMAND} -E echo "Running compiler tests..."
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Add individual test cases
foreach(test_file ${TEST_FILES})
    get_filename_component(test_name ${test_file} NAME_WE)

    add_test(
            NAME ${test_name}
            COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:clumsyc>
            -DTEST_FILE=${test_file}
            -DTEST_NAME=${test_name}
            -DTMP_DIR=${CMAKE_BINARY_DIR}/tmp
            -DTEST_DIR=${CMAKE_SOURCE_DIR}/tests
            -DSRC_DIR=${CMAKE_SOURCE_DIR}/src
            -P ${CMAKE_SOURCE_DIR}/test_runner.cmake
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

    # Make tests depend on the compiler being built
    set_tests_properties(${test_name} PROPERTIES
            DEPENDS clumsyc
    )
endforeach()

# Custom target for verbose testing
add_custom_target(test-verbose
        COMMAND ${CMAKE_COMMAND} -E echo "Running compiler tests verbose..."
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

foreach(test_file ${TEST_FILES})
    add_custom_command(TARGET test-verbose POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "=== Testing ${test_file} ==="
            COMMAND $<TARGET_FILE:clumsyc> ${test_file}
            COMMAND ${CMAKE_COMMAND} -E echo ""
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            VERBATIM
    )
endforeach()

# Clean target (CMake handles this automatically, but we can add custom cleanup)
add_custom_target(clean-all
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/tmp
        COMMAND ${CMAKE_COMMAND} -E remove -f test_output
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)