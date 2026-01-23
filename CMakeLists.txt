# CMakeList.txt : CMake project for trainingdata-tool, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
project(trainingdata-tool)

set(CMAKE_REQUIRED_FLAGS -std=c++20)

file(GLOB_RECURSE sources src/*.cpp src/*.h)

set (
    lc0
    "lc0/src/chess/board.cc"
    "lc0/src/chess/position.cc"
    "lc0/src/neural/encoder.cc"
    "lc0/src/trainingdata/writer.cc"
    "lc0/src/utils/commandline.cc"
    "lc0/src/utils/logging.cc"
    "lc0/src/utils/random.cc"
    "lc0/src/utils/string.cc"
)

if (WIN32)
    set (lc0_filesystem "lc0/src/utils/filesystem.win32.cc")
else (WIN32)
    set (lc0_filesystem "lc0/src/utils/filesystem.posix.cc")
endif (WIN32)

AUX_SOURCE_DIRECTORY(polyglot/src polyglot)

# Use system zlib on Unix to avoid old bundled zlib issues with modern compilers
if (UNIX)
    find_package(ZLIB REQUIRED)
    set(ZLIB_LIBS ZLIB::ZLIB)
    set(ZLIB_INCLUDE ${ZLIB_INCLUDE_DIRS})
else()
    AUX_SOURCE_DIRECTORY(zlib zlib_sources)
    set(ZLIB_LIBS "")
    set(ZLIB_INCLUDE "zlib")
endif()

# Add source to this project's executable.
if (UNIX)
    add_executable(trainingdata-tool ${sources} ${lc0} ${lc0_filesystem} ${polyglot})
else()
    add_executable(trainingdata-tool ${sources} ${lc0} ${lc0_filesystem} ${polyglot} ${zlib_sources})
endif()

set_target_properties(trainingdata-tool PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)

if (UNIX)
    target_link_libraries(trainingdata-tool -lpthread -lstdc++fs ${ZLIB_LIBS})
endif(UNIX)

set(CMAKE_BUILD_TYPE Release)

include_directories(
    "lc0/src"
    "src"
    "polyglot/src"
    ${ZLIB_INCLUDE}
    "."
)

# For MinGW: pre-define __GETOPT_H__ to prevent polyglot's getopt.h from
# being used when system unistd.h includes <getopt.h>. This avoids the
# "undefined reference to getopt_internal" error.
if (MINGW)
    add_compile_definitions(__GETOPT_H__)
endif()

add_compile_definitions(NO_PEXT)

# MSVC-specific settings for compatibility with legacy C code in polyglot and lc0 quirks
# MSVC-specific settings for compatibility with legacy C code in polyglot and lc0 quirks
if (MSVC)
    # Disable deprecation warnings for unsafe CRT functions and other noisy warnings
    # 4996: unsafe function (strcpy vs strcpy_s)
    # 4267, 4244: conversion loss of data
    # 4390: empty controlled statement
    # 4018: signed/unsigned mismatch
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/wd4996 /wd4267 /wd4244 /wd4390 /wd4018)
    
    # Relax conformance mode to allow some legacy C++ constructs
    add_compile_options(/permissive)
    
    # Force include <array> because lc0/src/neural/encoder.cc uses std::array but doesn't include it
    add_compile_options(/FIarray)

    # Specific flags for polyglot files to handle const char* conversions
    # We try to force them to use older C++ standard semantics where possible
    set_source_files_properties(${polyglot} PROPERTIES 
        COMPILE_OPTIONS "/Zc:strictStrings-;/permissive"
    )
elseif (MINGW)
    # MinGW/GCC also needs lenient handling for legacy C code
    set_source_files_properties(${polyglot} PROPERTIES 
        COMPILE_OPTIONS "-fpermissive;-Wno-write-strings"
    )
endif()

# Optional: Add Doxygen documentation support
find_package(Doxygen QUIET)
if (DOXYGEN_FOUND)
    # Set input and output directories
    set(DOXYGEN_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/docs)
    
    # Configure Doxygen
    set(DOXYGEN_PROJECT_NAME "Training Data Tool")
    set(DOXYGEN_PROJECT_BRIEF "Tool to generate lc0 training data from PGN files")
    set(DOXYGEN_OUTPUT_HTML YES)
    set(DOXYGEN_OUTPUT_LATEX NO)
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_EXTRACT_PRIV_VIRTUAL YES)
    set(DOXYGEN_GENERATE_TODOLIST YES)
    set(DOXYGEN_GENERATE_BUGLIST YES)
    
    # Create custom target for documentation
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    
    # Configure the Doxyfile
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
        ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        @ONLY
    )
    
    message(STATUS "Doxygen found - documentation target 'docs' available")
else()
    message(STATUS "Doxygen not found - documentation generation disabled")
endif()

# TODO: Add tests and install targets if needed.
