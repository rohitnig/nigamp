# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System and Commands

This is a C++ project using CMake and MinGW on Windows. The project targets <10MB RAM usage and provides high-quality audio playback.

### Required Dependencies
- MinGW-w64 with GCC
- CMake 3.16 or higher
- Git (for Google Test)
- dr_wav.h (downloaded automatically by `copy_drwav.bat`)
- minimp3.h (header-only, included in repository)

### Build Commands
```powershell
# Setup dependencies (run once)
.\setup_libraries.bat

# Standard build (with optional test prompt)
.\build.bat

# Debug build (enables DEBUG_LOG output)
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-DDEBUG"
cmake --build build

# Manual CMake build
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe
cmake --build build

# Run tests
ctest --test-dir build --verbose
```

### Test Commands
```powershell
# Run all unit tests
ctest --test-dir build --verbose

# Run specific test suites
.\build\tests\nigamp_tests.exe --gtest_filter="CallbackSimple.*"
.\build\tests\nigamp_tests.exe --gtest_filter="SimpleCallback.*"

# Manual test executables
.\build\test_hotkey_handler.exe
.\build\test_music_player_simulation.exe

# Test callback architecture (deadlock-free tests)
.\test_fixed_callbacks.bat

# Specific test scripts
.\test_hotkeys.bat
.\test_music_simulation.bat
```

## Architecture Overview

The application follows a dependency injection pattern with interface-based design for testability:

### Core Components
- **AudioEngine** (`include\\audio_engine.hpp`, `src\\audio_engine.cpp`) - DirectSound-based audio output with ~50ms latency target
- **Decoder** (`include\\mp3_decoder.hpp`, `src\\mp3_decoder.cpp`) - Pluggable MP3/WAV decoder using minimp3 and dr_wav  
- **Playlist** (`include\\playlist.hpp`, `src\\playlist.cpp`) - Fisher-Yates shuffle algorithm with bidirectional navigation
- **HotkeyHandler** (`include\\hotkey_handler.hpp`, `src\\hotkey_handler.cpp`) - Windows global hotkey system using RegisterHotKey API
- **FileScanner** (`include\\file_scanner.hpp`, `src\\file_scanner.cpp`) - Directory scanning with MP3/WAV format detection
- **MusicPlayer** (`src\\main.cpp`) - Main application orchestrating all components

### Design Patterns
- **RAII**: All components use proper resource management
- **Thread Safety**: Concurrent audio processing with mutex-protected playlist operations
- **Interface Segregation**: All core components implement abstract interfaces (`I*` classes)
- **Streaming Architecture**: Minimal memory allocations, audio data streamed in chunks

### Threading Model
- Main thread: UI updates and hotkey handling
- Playback thread: Duration tracking, countdown display, and audio streaming
- Audio engine thread: DirectSound buffer management and callback firing
- Timeout thread: Safety fallback for callback-based completion (3-second timeout)
- Reindexing thread: Background directory scanning every 10 minutes

## Key Implementation Details

### Duration-Based Completion System
The application uses a sophisticated duration-based completion system that solves the classic audio player problem of premature track advancement:

- **Duration Tracking**: Uses `IAudioDecoder::get_duration()` to get exact song length
- **Precise Timing**: Tracks `m_playback_start_time` and calculates elapsed vs. total duration
- **Smart Completion**: Continues until full song duration is reached, even after decoder EOF
- **Silence Padding**: Fills audio buffers with silence after decoder EOF to maintain timing accuracy

**Key Advantage**: Tracks advance exactly when songs finish playing, not when file decoding completes.

### Live Countdown Display
Real-time visual feedback system that updates on the same console line:

- **Dynamic Updates**: Refreshes every 500ms using carriage return (`\r`) for in-place updates
- **Time Formatting**: Displays `MM:SS` format with remaining/total time
- **Status Indicators**: Shows 🎵 for playing, ⏸️ [PAUSED] for paused, [PREVIEW] for preview mode
- **Clean Completion**: Automatically clears countdown line when songs finish

### Audio Pipeline
The audio engine uses DirectSound with circular buffering and dual completion detection. Audio data flows: File → Decoder → AudioBuffer → DirectSound buffer. Target latency is <50ms for responsive hotkey control.

**Dual Completion Detection**: Uses both DirectSound buffer position checking AND duration-based timing for bulletproof completion detection with 3-second timeout fallback.

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

## Command Line Usage

### Basic Usage Patterns
```powershell
# Basic usage - scans C:\Music directory
.\\build\\nigamp.exe

# Play specific file
.\\build\\nigamp.exe --file song.mp3
.\\build\\nigamp.exe -f "C:\\Music\\My Song.mp3"

# Play all files from directory  
.\\build\\nigamp.exe --folder "C:\\Music"
.\\build\\nigamp.exe -d "C:\\My Music Collection"

# Preview mode - play first 10 seconds of each song
.\\build\\nigamp.exe --preview
.\\build\\nigamp.exe -p

# Combine options
.\\build\\nigamp.exe -f song.mp3 -p    # Preview single file
.\\build\\nigamp.exe -d "C:\\Music" -p  # Preview entire directory

# Help
.\\build\\nigamp.exe --help
```

### Global Hotkeys (Work Anywhere)
- **Ctrl+Alt+N**: Next track
- **Ctrl+Alt+P**: Previous track  
- **Ctrl+Alt+R**: Pause/Resume
- **Ctrl+Alt+Plus**: Volume up
- **Ctrl+Alt+Minus**: Volume down
- **Ctrl+Alt+Escape**: Quit

### Local Hotkeys (Console Focused)
- **Ctrl+N**: Next track
- **Ctrl+P**: Previous track
- **Ctrl+R**: Pause/Resume
- **Ctrl+Plus**: Volume up
- **Ctrl+Minus**: Volume down
- **Ctrl+Escape**: Quit

## VSCode Integration

The project includes full VSCode integration:
- IntelliSense configuration via `.vscode\\c_cpp_properties.json`
- Build and debug tasks in `.vscode\\tasks.json`
- CMake integration with configured settings
- Test runner configuration for unit tests

## Debug Logging System

The codebase includes a structured logging framework with conditional compilation:

```cpp
#define DEBUG_LOG(msg)  // Only active when compiled with -DDEBUG
#define INFO_LOG(msg)   // General information output
#define ERROR_LOG(msg)  // Error messages to stderr
```

- **Release Mode**: Only `INFO_LOG` and `ERROR_LOG` messages appear
- **Debug Mode**: All logging levels active for detailed debugging
- **Performance**: Zero overhead in release builds via macro elimination

## Critical Implementation Notes

### Duration-Based vs EOF-Based Completion
The system now prioritizes duration-based completion over decoder EOF detection:

- **Problem Solved**: Eliminates premature track advancement when file decoding finishes before audio playback
- **Implementation**: Continues playback with silence padding until `elapsed_seconds >= song_duration`
- **User Experience**: Natural track transitions that match actual audio completion
- **Fallback Safety**: 3-second timeout prevents infinite loops if duration detection fails

### Enhanced Callback Architecture
The `CompletionCallback` mechanism includes dual verification methods:
- **DirectSound Buffer Position**: Checks if hardware play cursor has caught up to write position
- **Time-Based Completion**: Calculates remaining playback time based on buffer size and sample rate
- **Structured Results**: `CompletionResult` provides detailed completion information with timing metrics
- **Error Handling**: Comprehensive error codes with descriptive messages

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
- Test duration-based completion with `CallbackSimple.*` test suites specifically designed to avoid deadlocks

### Development Workflow Notes
- **Duration Testing**: Use short test audio files (1-2 seconds) to verify completion timing
- **Countdown Display**: Test with both normal and preview modes to ensure proper formatting
- **Debug Output**: Enable DEBUG_LOG during development to trace timing and completion logic
- **Thread Safety**: Pay special attention to duration tracking variables in multi-threaded contexts