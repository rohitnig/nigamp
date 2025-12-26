cmake_minimum_required(VERSION 3.16)

# Set compiler hints for MinGW (Windows only)
if(WIN32 AND NOT CMAKE_CXX_COMPILER)
    if(EXISTS "C:/mingw64/bin/g++.exe")
        set(CMAKE_CXX_COMPILER "C:/mingw64/bin/g++.exe")
        set(CMAKE_C_COMPILER "C:/mingw64/bin/gcc.exe")
    endif()
endif()

project(nigamp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable testing
enable_testing()

# Compiler-specific options for MinGW (Windows)
if(MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -static-libgcc -static-libstdc++")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/third_party)

# Check for required libraries
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/dr_wav.h")
    if(WIN32)
        message(WARNING "dr_wav.h not found in third_party directory. Please run copy_drwav.bat or setup_libraries.bat first.")
    else()
        message(WARNING "dr_wav.h not found in third_party directory. Please run ./setup_libraries.sh first.")
    endif()
endif()

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/minimp3.h")
    message(WARNING "minimp3.h not found in third_party directory.")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/mp3_decoder.cpp
    src/playlist.cpp
    src/file_scanner.cpp
)

# Platform-specific source files
if(WIN32)
    list(APPEND SOURCES
        src/audio_engine.cpp
        src/hotkey_handler.cpp
    )
elseif(UNIX AND NOT APPLE)
    # Linux
    list(APPEND SOURCES
        src/alsa_audio_engine.cpp
        src/linux_hotkey_handler.cpp
    )
endif()

# Headers
set(HEADERS
    include/audio_engine.hpp
    include/mp3_decoder.hpp
    include/playlist.hpp
    include/hotkey_handler.hpp
    include/file_scanner.hpp
    include/types.hpp
)

# Main executable
add_executable(nigamp ${SOURCES} ${HEADERS})

# minimp3 is header-only, no library linking needed
message(STATUS "Using minimp3 header-only MP3 decoder")

# Platform-specific libraries
if(WIN32)
    target_link_libraries(nigamp winmm dsound ole32 user32 shlwapi)
    message(STATUS "Building for Windows with DirectSound")
elseif(UNIX AND NOT APPLE)
    # Linux - find ALSA
    find_library(ASOUND_LIBRARY asound)
    if(ASOUND_LIBRARY)
        target_link_libraries(nigamp ${ASOUND_LIBRARY})
        message(STATUS "Building for Linux with ALSA")
    else()
        message(FATAL_ERROR "ALSA library not found. Install libasound2-dev: sudo apt-get install libasound2-dev")
    endif()
endif()

# Test executable
add_subdirectory(tests)

# Installation
install(TARGETS nigamp DESTINATION bin)