# CMakeList.txt : CMake project for trainingdata-tool, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
project(trainingdata-tool)

# Check for required dependencies
find_package(Threads REQUIRED)

# Check for C++20 support
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Check for filesystem support
if(CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "C++17 or higher is required for filesystem support")
endif()

# Check if we have std::filesystem
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <filesystem>
int main() { std::filesystem::path p; return 0; }
" HAVE_STD_FILESYSTEM)

if(NOT HAVE_STD_FILESYSTEM)
    message(STATUS "Using experimental filesystem library")
    add_compile_definitions(USE_EXPERIMENTAL_FILESYSTEM)
endif()

# Explicitly list source files instead of using GLOB_RECURSE
set(sources
    "src/Config.cpp"
    "src/PGNGame.cpp"
    "src/PGNMoveInfo.cpp"
    "src/StaticEvaluator.cpp"
    "src/TrainingDataDedup.cpp"
    "src/TrainingDataReader.cpp"
    "src/TrainingDataWriter.cpp"
    "src/polyglot_lib.cpp"
    "src/trainingdata-tool.cpp"
    "src/trainingdata.cpp"
)

set(headers
    "src/Config.h"
    "src/PGNGame.h"
    "src/PGNMoveInfo.h"
    "src/StaticEvaluator.h"
    "src/TrainingDataDedup.h"
    "src/TrainingDataReader.h"
    "src/TrainingDataWriter.h"
    "src/polyglot_lib.h"
    "src/trainingdata.h"
    "src/V6TrainingDataHashUtil.h"
)

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

# Set target properties
set_target_properties(trainingdata-tool PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if (UNIX)
    target_link_libraries(trainingdata-tool Threads::Threads -lstdc++fs ${ZLIB_LIBS})
elseif (WIN32)
    target_link_libraries(trainingdata-tool ${ZLIB_LIBS})
endif()

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

# MSVC-specific settings for compatibility with legacy C code in polyglot and lc0
if (MSVC)
    # Enable all warnings but treat most as warnings, not errors
    add_compile_options(/W3)
    
    # Disable specific warnings that are noisy but not critical
    # 4996: unsafe function (strcpy vs strcpy_s) - legacy code uses these
    # 4267: size_t to int conversion - common in chess engine code
    # 4244: conversion loss of data - similar to above
    # 4390: empty controlled statement - common in macros
    # 4018: signed/unsigned mismatch - frequent in chess bitboard code
    # 4819: characters that cannot be represented in current code page
    add_compile_options(/wd4996 /wd4267 /wd4244 /wd4390 /wd4018 /wd4819)
    
    # Use secure CRT functions where possible
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    
    # Force include necessary headers for lc0 compatibility
    add_compile_options(/FIarray)
    
    # Set proper character encoding
    add_compile_options(/utf-8)
    
    # Specific flags for polyglot files to handle const char* conversions
    # These files are legacy C code that needs more permissive compilation
    set_source_files_properties(${polyglot} PROPERTIES 
        COMPILE_OPTIONS "/Zc:strictStrings-;/W1"
    )
    
    # Additional compatibility flags for lc0 source files
    set_source_files_properties(${lc0} PROPERTIES
        COMPILE_OPTIONS "/W2;/permissive-"
    )
    
elseif (MINGW)
    # MinGW/GCC settings for Windows compatibility
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    
    # Handle legacy C code more permissively
    set_source_files_properties(${polyglot} PROPERTIES 
        COMPILE_OPTIONS "-fpermissive;-Wno-write-strings;-Wno-unused-result"
    )
    
else()
    # Linux/Unix settings
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    
    # Legacy C code handling for polyglot
    set_source_files_properties(${polyglot} PROPERTIES 
        COMPILE_OPTIONS "-Wno-write-strings;-Wno-unused-result;-Wno-sign-compare"
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
