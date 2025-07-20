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
    test_audio_engine.cpp
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

# Windows-specific libraries for tests
if(WIN32)
    target_link_libraries(nigamp_tests winmm dsound ole32 user32)
endif()

# Compiler-specific options for MinGW
if(MINGW)
    target_compile_options(nigamp_tests PRIVATE -static-libgcc -static-libstdc++)
    target_link_options(nigamp_tests PRIVATE -static)
endif()

# Discover tests
include(GoogleTest)
gtest_discover_tests(nigamp_tests)