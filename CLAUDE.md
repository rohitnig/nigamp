# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System and Commands

This is a C++ project using CMake and MinGW on Windows. The project targets <10MB RAM usage and provides high-quality audio playback.

### Required Dependencies
- MinGW-w64 with GCC
- CMake 3.16 or higher
- Git (for Google Test)
- dr_wav.h (downloaded automatically by `copy_drwav.bat`)
- libmpg123 (manual download required)

### Build Commands
```bash
# Setup dependencies (run once)
setup_libraries.bat

# Standard build
build.bat

# Manual CMake build
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe
cmake --build build

# Run tests
ctest --test-dir build --verbose
```

### Test Commands
```bash
# Run all unit tests
ctest --test-dir build

# Run specific test suites
build\tests\nigamp_tests.exe --gtest_filter="CallbackSimple.*"
build\tests\nigamp_tests.exe --gtest_filter="SimpleCallback.*"

# Manual test executables
build\test_hotkey_handler.exe
build\test_music_player_simulation.exe

# Test callback architecture (deadlock-free tests)
test_fixed_callbacks.bat

# Specific test scripts
test_hotkeys.bat
test_music_simulation.bat
```

## Architecture Overview

The application follows a dependency injection pattern with interface-based design for testability:

### Core Components
- **AudioEngine** (`audio_engine.hpp/cpp`) - DirectSound-based audio output with ~50ms latency target
- **Decoder** (`mp3_decoder.hpp/cpp`) - Pluggable MP3/WAV decoder using minimp3 and dr_wav  
- **Playlist** (`playlist.hpp/cpp`) - Fisher-Yates shuffle algorithm with bidirectional navigation
- **HotkeyHandler** (`hotkey_handler.hpp/cpp`) - Windows global hotkey system using RegisterHotKey API
- **FileScanner** (`file_scanner.hpp/cpp`) - Directory scanning with MP3/WAV format detection
- **MusicPlayer** (`main.cpp`) - Main application orchestrating all components

### Design Patterns
- **RAII**: All components use proper resource management
- **Thread Safety**: Concurrent audio processing with mutex-protected playlist operations
- **Interface Segregation**: All core components implement abstract interfaces (`I*` classes)
- **Streaming Architecture**: Minimal memory allocations, audio data streamed in chunks

### Threading Model
- Main thread: UI and hotkey handling
- Playback thread: Audio decoding and DirectSound buffer management  
- Reindexing thread: Background directory scanning every 10 minutes
- Timeout thread: Safety fallback for callback-based track advancement (3-second timeout)

## Key Implementation Details

### Audio Pipeline
The audio engine uses DirectSound with circular buffering and callback-based completion detection. Audio data flows: File → Decoder → AudioBuffer → DirectSound buffer. Target latency is <50ms for responsive hotkey control.

**Callback Architecture**: Track advancement uses a callback-based system where the audio engine notifies the music player when audio playback actually completes (not just when file decoding finishes). This prevents premature track advancement and includes a 3-second timeout fallback for safety.

### Hotkey System
Supports both global hotkeys (Ctrl+Alt+*) and local console hotkeys (Ctrl+*). Uses Windows RegisterHotKey API for global shortcuts.

### Memory Management
Targets <10MB RAM through streaming audio, minimal buffering, and efficient playlist structures. Uses smart pointers throughout.

### File Handling
Supports MP3 (via minimp3) and WAV (via dr_wav) with automatic format detection. Includes background directory reindexing.

## Testing Strategy

The codebase uses Google Test with comprehensive unit test coverage:
- Component isolation through dependency injection
- Mock implementations for audio and hotkey testing (deadlock-free mock engines)
- Separate test executables for interactive components (hotkeys, music simulation)
- TDD approach with test-first development
- Callback architecture testing with `CallbackSimple.*` and `SimpleCallback.*` test suites
- Integration testing with real audio files for end-to-end validation

**Important**: Use `CallbackSimple.*` tests for callback mechanism validation - these avoid mutex deadlocks that can occur in complex mock implementations.

## Third-Party Dependencies

All dependencies are header-only or statically linked:
- `minimp3.h` - MP3 decoding (header-only)
- `dr_wav.h` - WAV decoding (header-only)  
- Google Test - Unit testing framework (fetched by CMake)
- Windows APIs - DirectSound, User32 (system libraries)

## Critical Implementation Notes

### Audio Completion Detection
The audio engine implements a callback-based completion system that distinguishes between:
- **File decoding completion** (when decoder reaches EOF)  
- **Audio playback completion** (when audio engine buffers are actually empty)

This prevents the common issue of tracks advancing prematurely before audio finishes playing. The `CompletionCallback` mechanism includes structured error reporting via `CompletionResult` with timing metrics and error codes.

### Threading Safety Patterns
- Use atomic flags for cross-thread communication (`m_advance_to_next`, `m_should_quit`)
- Single mutex per component to avoid deadlocks (never nest mutex locks)
- Callback firing uses atomic exchange patterns to prevent double-execution
- Timeout threads provide safety fallbacks for callback mechanisms

### Mock Testing Guidelines
When creating mock implementations for testing:
- Use atomic variables instead of mutexes where possible
- Avoid nested lock acquisition in callback paths
- Use `SimpleMockEngine` pattern for deadlock-free testing
- Implement completion checking without holding multiple locks simultaneously