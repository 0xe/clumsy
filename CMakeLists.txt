cmake_minimum_required(VERSION 3.16)
project(clumsyc VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

# --- Compiler ---
set(COMPILER_SOURCES
    src/main.c
    src/tokenizer.c
    src/parser.c
    src/compiler.c
)

set(COMPILER_HEADERS
    src/types.h
    src/tokenizer.h
    src/parser.h
    src/compiler.h
)

add_executable(clumsyc ${COMPILER_SOURCES} ${COMPILER_HEADERS})
set_target_properties(clumsyc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# --- Print helpers (linked into generated programs, not the compiler) ---
add_library(print_helpers STATIC src/print_helpers.c)
set_target_properties(print_helpers PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# --- Tests ---
enable_testing()

file(GLOB TEST_FILES "${CMAKE_SOURCE_DIR}/tests/*.cpl")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tmp)

foreach(test_file ${TEST_FILES})
    get_filename_component(test_name ${test_file} NAME_WE)

    add_test(
        NAME ${test_name}
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:clumsyc>
            -DTEST_FILE=${test_file}
            -DTMP_DIR=${CMAKE_BINARY_DIR}/tmp
            -DTEST_DIR=${CMAKE_SOURCE_DIR}/tests
            -DPRINT_HELPERS=$<TARGET_FILE:print_helpers>
            -P ${CMAKE_SOURCE_DIR}/test_runner.cmake
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(${test_name} PROPERTIES DEPENDS "clumsyc;print_helpers")
endforeach()

# --- Examples ---
file(GLOB EXAMPLE_FILES "${CMAKE_SOURCE_DIR}/examples/*.cpl")

foreach(example_file ${EXAMPLE_FILES})
    get_filename_component(example_name ${example_file} NAME_WE)

    set(EXTRA_LINK_LIBS "")
    if(${example_name} MATCHES "^(tcp_server|tcp_client)$")
        set(EXTRA_LINK_LIBS "-L/opt/homebrew/lib -lSDL2 -lSDL2_net")
    elseif(${example_name} MATCHES "^(sdl_demo|sdl_loop|sdl_bindings|real_tetris)$")
        set(EXTRA_LINK_LIBS "-L/opt/homebrew/lib -lSDL2")
    endif()

    add_custom_target(build-${example_name}
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:clumsyc>
            -DEXAMPLE_FILE=${example_file}
            -DTMP_DIR=${CMAKE_BINARY_DIR}/tmp
            -DPRINT_HELPERS=$<TARGET_FILE:print_helpers>
            -DLINK_LIBS=${EXTRA_LINK_LIBS}
            -P ${CMAKE_SOURCE_DIR}/example_runner.cmake
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS clumsyc print_helpers
        COMMENT "Building example: ${example_name}"
    )
endforeach()

add_custom_target(examples
    DEPENDS print_helpers
    COMMENT "Building all examples"
)
foreach(example_file ${EXAMPLE_FILES})
    get_filename_component(example_name ${example_file} NAME_WE)
    add_dependencies(examples build-${example_name})
endforeach()

# --- Clean ---
add_custom_target(clean-extra
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/tmp
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
)
