cmake_minimum_required(VERSION 3.16)

# Google Test setup
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.12.1
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Test source files
set(TEST_SOURCES
    test_playlist.cpp
    test_file_scanner.cpp
    test_decoder.cpp
    test_callback_simple.cpp
    test_callback_architecture.cpp
)

# Platform-specific audio engine test
if(WIN32)
    list(APPEND TEST_SOURCES test_audio_engine.cpp)
endif()

# Standalone hotkey test (separate executable)
if(WIN32)
    add_executable(test_hotkey_handler 
        test_hotkey_handler.cpp
        ${CMAKE_SOURCE_DIR}/src/hotkey_handler.cpp
    )
    target_link_libraries(test_hotkey_handler user32)
elseif(UNIX AND NOT APPLE)
    add_executable(test_hotkey_handler 
        test_hotkey_handler.cpp
        ${CMAKE_SOURCE_DIR}/src/linux_hotkey_handler.cpp
    )
endif()

# Include directories for hotkey test
target_include_directories(test_hotkey_handler PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Music player simulation test (separate executable)
if(WIN32)
    add_executable(test_music_player_simulation 
        test_music_player_simulation.cpp
        ${CMAKE_SOURCE_DIR}/src/hotkey_handler.cpp
        ${CMAKE_SOURCE_DIR}/src/playlist.cpp
    )
    target_link_libraries(test_music_player_simulation user32)
elseif(UNIX AND NOT APPLE)
    add_executable(test_music_player_simulation 
        test_music_player_simulation.cpp
        ${CMAKE_SOURCE_DIR}/src/linux_hotkey_handler.cpp
        ${CMAKE_SOURCE_DIR}/src/playlist.cpp
    )
endif()

# Include directories for music player test
target_include_directories(test_music_player_simulation PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Create test executable
add_executable(nigamp_tests ${TEST_SOURCES})

# Link libraries
target_link_libraries(nigamp_tests
    gtest_main
    gtest
)

# Include directories
target_include_directories(nigamp_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Platform-specific libraries for tests
if(WIN32)
    target_link_libraries(nigamp_tests winmm dsound ole32 user32)
elseif(UNIX AND NOT APPLE)
    find_library(ASOUND_LIBRARY asound)
    if(ASOUND_LIBRARY)
        target_link_libraries(nigamp_tests ${ASOUND_LIBRARY})
    endif()
endif()

# Compiler-specific options for MinGW
if(MINGW)
    target_compile_options(nigamp_tests PRIVATE -static-libgcc -static-libstdc++)
    target_link_options(nigamp_tests PRIVATE -static)
endif()

# Discover tests
include(GoogleTest)
gtest_discover_tests(nigamp_tests)