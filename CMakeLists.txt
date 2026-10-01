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
        src/null_media_controls.cpp
    )
elseif(UNIX AND NOT APPLE)
    # Linux
    list(APPEND SOURCES
        src/alsa_audio_engine.cpp
        src/linux_hotkey_handler.cpp
    )

    # MPRIS media controls (media keys, GNOME media widget) need sd-bus from libsystemd.
    # This is the only working global control on Wayland sessions; optional otherwise.
    find_path(SDBUS_INCLUDE_DIR systemd/sd-bus.h)
    find_library(SYSTEMD_LIBRARY systemd)
    if(SDBUS_INCLUDE_DIR AND SYSTEMD_LIBRARY)
        list(APPEND SOURCES src/mpris_media_controls.cpp)
        set(NIGAMP_HAS_MPRIS ON)
    else()
        list(APPEND SOURCES src/null_media_controls.cpp)
        message(WARNING "libsystemd (sd-bus) not found - media keys/MPRIS disabled. Install libsystemd-dev: sudo apt-get install libsystemd-dev")
    endif()
endif()

# Optional mini-player window (--gui) built with FLTK 1.4
option(NIGAMP_GUI "Build the FLTK mini-player window (--gui)" ON)
set(NIGAMP_FLTK_TARGET "")
if(NIGAMP_GUI)
    if(UNIX)
        # Use fltk-config: Debian/Ubuntu's FLTKConfig.cmake has unexpanded ${DEB_HOST_MULTIARCH} paths
        find_program(FLTK_CONFIG fltk-config)
        if(FLTK_CONFIG)
            execute_process(COMMAND ${FLTK_CONFIG} --version
                OUTPUT_VARIABLE NIGAMP_FLTK_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
    endif()
    if(NIGAMP_FLTK_VERSION AND NIGAMP_FLTK_VERSION VERSION_GREATER_EQUAL 1.4)
        execute_process(COMMAND ${FLTK_CONFIG} --cxxflags
            OUTPUT_VARIABLE NIGAMP_FLTK_CXXFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND ${FLTK_CONFIG} --ldflags
            OUTPUT_VARIABLE NIGAMP_FLTK_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
        separate_arguments(NIGAMP_FLTK_CXXFLAGS UNIX_COMMAND "${NIGAMP_FLTK_CXXFLAGS}")
        separate_arguments(NIGAMP_FLTK_LDFLAGS UNIX_COMMAND "${NIGAMP_FLTK_LDFLAGS}")
        add_library(nigamp_fltk INTERFACE)
        target_compile_options(nigamp_fltk INTERFACE ${NIGAMP_FLTK_CXXFLAGS})
        target_link_libraries(nigamp_fltk INTERFACE ${NIGAMP_FLTK_LDFLAGS})
        set(NIGAMP_FLTK_TARGET nigamp_fltk)
    else()
        find_package(FLTK 1.4 CONFIG QUIET)
    endif()
    if(NIGAMP_FLTK_TARGET)
        # found via fltk-config
    elseif(FLTK_FOUND)
        if(TARGET fltk::fltk)
            set(NIGAMP_FLTK_TARGET fltk::fltk)
        else()
            set(NIGAMP_FLTK_TARGET fltk)
        endif()
    elseif(WIN32)
        # No system FLTK on Windows: fetch and build it statically (needs network on first configure)
        include(FetchContent)
        set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
        set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
        set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "" FORCE)
        set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(FLTK_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(fltk
            GIT_REPOSITORY https://github.com/fltk/fltk.git
            GIT_TAG        release-1.4.4
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(fltk)
        set(NIGAMP_FLTK_TARGET fltk::fltk)
    endif()
endif()

if(NIGAMP_FLTK_TARGET)
    list(APPEND SOURCES src/fltk_player_gui.cpp)
    message(STATUS "FLTK found - mini-player GUI (--gui) enabled")
else()
    list(APPEND SOURCES src/null_player_gui.cpp)
    if(NIGAMP_GUI)
        message(WARNING "FLTK 1.4 not found - GUI (--gui) disabled. Install libfltk1.4-dev: sudo apt-get install libfltk1.4-dev")
    endif()
endif()

# Headers
set(HEADERS
    include/audio_engine.hpp
    include/mp3_decoder.hpp
    include/playlist.hpp
    include/hotkey_handler.hpp
    include/file_scanner.hpp
    include/media_controls.hpp
    include/player_gui.hpp
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
    # Linux - find ALSA and X11
    find_library(ASOUND_LIBRARY asound)
    find_library(X11_LIBRARY X11)
    if(ASOUND_LIBRARY)
        target_link_libraries(nigamp ${ASOUND_LIBRARY})
        message(STATUS "Building for Linux with ALSA")
    else()
        message(FATAL_ERROR "ALSA library not found. Install libasound2-dev: sudo apt-get install libasound2-dev")
    endif()
    if(NIGAMP_HAS_MPRIS)
        target_include_directories(nigamp PRIVATE ${SDBUS_INCLUDE_DIR})
        target_link_libraries(nigamp ${SYSTEMD_LIBRARY})
        message(STATUS "libsystemd found - MPRIS media controls enabled")
    endif()
    if(X11_LIBRARY)
        target_link_libraries(nigamp ${X11_LIBRARY})
        message(STATUS "X11 library found - global hotkeys enabled")
    else()
        message(WARNING "X11 library not found. Global hotkeys will not work. Install libx11-dev: sudo apt-get install libx11-dev")
    endif()
endif()

if(NIGAMP_FLTK_TARGET)
    target_link_libraries(nigamp ${NIGAMP_FLTK_TARGET})
endif()

# Test executable
add_subdirectory(tests)

# Installation
install(TARGETS nigamp DESTINATION bin)