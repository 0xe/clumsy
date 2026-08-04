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
    src/ir.c
    src/ir_build.c
    src/emit_qbe.c
    src/emit_llvm.c
    src/emit_cranelift.c
    src/stubs.c
    src/cfg.c
)

set(COMPILER_HEADERS
    src/types.h
    src/tokenizer.h
    src/parser.h
    src/compiler.h
    src/ir.h
    src/cfg.h
)

add_executable(clumsyc ${COMPILER_SOURCES} ${COMPILER_HEADERS})
set_target_properties(clumsyc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# --- Tests ---
enable_testing()

if(NOT DEFINED TEST_BACKEND)
    set(TEST_BACKEND "qbe")
endif()

# LLVM toolchain (llc) for the llvm test backend
if(NOT DEFINED LLC)
    find_program(LLC_EXE llc)
    if(NOT LLC_EXE)
        set(LLC_EXE "/opt/homebrew/opt/llvm/bin/llc")
    endif()
    set(LLC ${LLC_EXE})
endif()

# Cranelift helper (clifc) for the cranelift test backend
if(NOT DEFINED CLIFC)
    find_program(CLIFC_EXE clifc)
    if(NOT CLIFC_EXE)
        set(CLIFC_EXE "${CMAKE_SOURCE_DIR}/tools/clifc/target/release/clifc")
    endif()
    set(CLIFC ${CLIFC_EXE})
endif()

file(GLOB TEST_FILES "${CMAKE_SOURCE_DIR}/tests/*.cpl")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tmp)

foreach(test_file ${TEST_FILES})
    get_filename_component(test_name ${test_file} NAME_WE)

    add_test(
        NAME ${test_name}
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:clumsyc>
            -DLLC=${LLC}
            -DCLIFC=${CLIFC}
            -DTEST_FILE=${test_file}
            -DTMP_DIR=${CMAKE_BINARY_DIR}/tmp
            -DTEST_DIR=${CMAKE_SOURCE_DIR}/tests
            -DBACKEND=${TEST_BACKEND}
            -P ${CMAKE_SOURCE_DIR}/test_runner.cmake
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(${test_name} PROPERTIES DEPENDS "clumsyc")
endforeach()

# --- Examples ---
# Each subdirectory under examples/ is a separate example target.
# All .cpl files in a directory are compiled and linked together.

file(GLOB EXAMPLE_SUBDIRS LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/examples/*")

set(ALL_EXAMPLE_TARGETS "")

foreach(example_dir ${EXAMPLE_SUBDIRS})
    if(NOT IS_DIRECTORY ${example_dir})
        continue()
    endif()

    get_filename_component(example_name ${example_dir} NAME)
    file(GLOB CPL_FILES "${example_dir}/*.cpl")

    if(NOT CPL_FILES)
        continue()
    endif()

    # Determine link libs based on directory name
    set(EXTRA_LINK_LIBS "")
    if(${example_name} MATCHES "^tcp")
        set(EXTRA_LINK_LIBS "-L/opt/homebrew/lib -lSDL2 -lSDL2_net")
    elseif(${example_name} MATCHES "^(sdl|tetris)$")
        set(EXTRA_LINK_LIBS "-L/opt/homebrew/lib -lSDL2")
    endif()

    add_custom_target(build-${example_name}
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:clumsyc>
            -DEXAMPLE_DIR=${example_dir}
            "-DTMP_DIR=${CMAKE_BINARY_DIR}/tmp"
            "-DLINK_LIBS=${EXTRA_LINK_LIBS}"
            -P ${CMAKE_SOURCE_DIR}/example_runner.cmake
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS clumsyc
        COMMENT "Building example: ${example_name}"
    )
    list(APPEND ALL_EXAMPLE_TARGETS build-${example_name})

    # Per-backend variants (default build-${example_name} above is qbe)
    set(EXAMPLE_BACKENDS qbe aarch64-legacy llvm cranelift)
    foreach(backend ${EXAMPLE_BACKENDS})
        add_custom_target(build-${example_name}-${backend}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:clumsyc>
                -DEXAMPLE_DIR=${example_dir}
                "-DTMP_DIR=${CMAKE_BINARY_DIR}/tmp"
                "-DLINK_LIBS=${EXTRA_LINK_LIBS}"
                "-DBACKEND=${backend}"
                "-DLLC=${LLC}"
                "-DCLIFC=${CLIFC}"
                -P ${CMAKE_SOURCE_DIR}/example_runner.cmake
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            DEPENDS clumsyc
            COMMENT "Building example: ${example_name} (${backend})"
        )
        list(APPEND ALL_EXAMPLE_BACKEND_TARGETS build-${example_name}-${backend})
    endforeach()
endforeach()

add_custom_target(examples
    COMMENT "Building all examples"
)
foreach(target ${ALL_EXAMPLE_TARGETS})
    add_dependencies(examples ${target})
endforeach()

add_custom_target(examples-all-backends
    COMMENT "Building all examples with all backends"
)
foreach(target ${ALL_EXAMPLE_BACKEND_TARGETS})
    add_dependencies(examples-all-backends ${target})
endforeach()

# --- Clean ---
add_custom_target(clean-extra
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/tmp
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
)
