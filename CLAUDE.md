# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System and Commands

This is a cross-platform C++ project using CMake. Targets <10MB RAM usage and provides high-quality audio playback on both Windows and Linux.

### Platform-Specific Dependencies

**Windows:**
- MinGW-w64 with GCC
- CMake 3.16 or higher
- Git (for Google Test)
- Windows APIs: DirectSound, User32

**Linux (Ubuntu/Debian):**
- build-essential (GCC/G++)
- CMake 3.16 or higher
- libasound2-dev (ALSA audio library)
- libx11-dev (X11 for global hotkeys)
- Git (for Google Test)

**Shared Dependencies:**
- dr_wav.h (downloaded automatically by setup scripts)
- minimp3.h (header-only, included in repository)

### Build Commands

**Windows:**
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

**Linux:**
```bash
# Install system dependencies
sudo apt-get update
sudo apt-get install -y build-essential cmake libasound2-dev libx11-dev git

# Setup dependencies (run once)
./setup_libraries.sh

# Standard build (with optional test prompt)
./build.sh

# Debug build (enables DEBUG_LOG output)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-DDEBUG"
cmake --build build

# Manual CMake build
cmake -B build
cmake --build build

# Run tests
ctest --test-dir build --verbose
```

### Test Commands

**Windows:**
```powershell
# Run all unit tests
ctest --test-dir build --verbose

# Run specific test suites
.\build\tests\nigamp_tests.exe --gtest_filter="CallbackSimple.*"
.\build\tests\nigamp_tests.exe --gtest_filter="CallbackArchitecture.*"

# Manual test executables
.\build\test_hotkey_handler.exe
.\build\test_music_player_simulation.exe

# Test callback architecture (deadlock-free tests)
.\test_fixed_callbacks.bat

# Specific test scripts
.\test_hotkeys.bat
.\test_music_simulation.bat
```

**Linux:**
```bash
# Run all unit tests
ctest --test-dir build --verbose

# Run specific test suites
./build/tests/nigamp_tests --gtest_filter="CallbackSimple.*"
./build/tests/nigamp_tests --gtest_filter="CallbackArchitecture.*"

# Manual test executables
./build/test_hotkey_handler
./build/test_music_player_simulation

# Integration testing
./build/nigamp --file "tests/data/test1.mp3" --preview
```

## Architecture Overview

The application follows a dependency injection pattern with interface-based design for testability and cross-platform support.

### Core Components
- **AudioEngine** (`include/audio_engine.hpp`) - Platform-specific audio output with ~50ms latency target
  - Windows: `src/audio_engine.cpp` (DirectSound-based)
  - Linux: `src/alsa_audio_engine.cpp` (ALSA-based)
- **Decoder** (`include/mp3_decoder.hpp`, `src/mp3_decoder.cpp`) - Pluggable MP3/WAV decoder using minimp3 and dr_wav
- **Playlist** (`include/playlist.hpp`, `src/playlist.cpp`) - Fisher-Yates shuffle algorithm with bidirectional navigation
- **HotkeyHandler** (`include/hotkey_handler.hpp`) - Platform-specific hotkey system
  - Windows: `src/hotkey_handler.cpp` (RegisterHotKey API)
  - Linux: `src/linux_hotkey_handler.cpp` (X11 global hotkeys + terminal fallback)
- **FileScanner** (`include/file_scanner.hpp`, `src/file_scanner.cpp`) - Directory scanning with MP3/WAV format detection
- **MusicPlayer** (`src/main.cpp`) - Main application orchestrating all components

### Design Patterns
- **RAII**: All components use proper resource management
- **Thread Safety**: Concurrent audio processing with mutex-protected playlist operations
- **Interface Segregation**: All core components implement abstract interfaces (`I*` classes)
- **Streaming Architecture**: Minimal memory allocations, audio data streamed in chunks

### Threading Model
- Main thread: UI updates and hotkey handling
- Playback thread (`MusicPlayer::playback_loop`): throttled decoding, countdown display, preview limit, completion safety timeout
- Audio engine thread: Platform-specific buffer management (DirectSound/ALSA) and completion callback firing
- Reindexing thread: Background directory scanning every 10 minutes

## Key Implementation Details

### Track Completion (EOF + device drain)
A track ends when the decoder hits EOF **and** the audio device has played everything queued — not when the song's nominal duration elapses (MP3 durations are bitrate estimates).

1. **Backpressure**: `playback_loop` decodes ~100ms chunks (`DECODE_CHUNK_MS`) only while `get_buffered_samples()` is below ~500ms (`MAX_QUEUED_AUDIO_MS`). Without this the decoder races ahead, the whole song lands in RAM, and EOF arrives seconds before the audio does.
2. **EOF**: when `decoder->is_eof()`, the loop calls `signal_eof()` on the engine.
3. **Drain**: the engine fires `CompletionCallback` once its pending queue is empty and the device has drained (ALSA: `snd_pcm_avail()` reports a full buffer or `-EPIPE`; DirectSound: play cursor). The callback calls `request_advance()`.
4. **Safety net**: if no callback arrives within `COMPLETION_TIMEOUT_SECONDS` of *playing* time after EOF (pauses excluded), the loop forces an advance. Seeing the timeout message in normal playback means completion detection is broken.

Preview mode stops after 10s of playing time (pauses excluded) and advances directly. `get_duration()` is used only for the countdown display.

### Live Countdown Display
Real-time visual feedback system that updates on the same console line:

- **Dynamic Updates**: Refreshes every 500ms using carriage return (`\r`) for in-place updates
- **Time Formatting**: Displays `MM:SS` format with remaining/total time
- **Status Indicators**: Shows 🎵 for playing, ⏸️ [PAUSED] for paused, [PREVIEW] for preview mode
- **Clean Completion**: Automatically clears countdown line when songs finish
- Elapsed time excludes pauses; for MP3 the total is an estimate, so the countdown may not land exactly on 00:00

### Audio Pipeline
The audio engine uses platform-specific APIs with circular buffering and dual completion detection:
- **Windows**: File → Decoder → AudioBuffer → DirectSound buffer
- **Linux**: File → Decoder → AudioBuffer → ALSA PCM buffer

Target latency is <50ms for responsive hotkey control.

The ALSA device buffer is 500ms with 50ms periods; at most ~500ms more is queued in the engine's `pending_samples`, so stop/pause latency stays bounded.

### Hotkey System
**Windows**: Global hotkeys (Ctrl+Alt+*) and local console hotkeys (Ctrl+*) using RegisterHotKey API.

**Linux**: Global hotkeys (Ctrl+Alt+*) via X11 with automatic fallback to terminal input (N/P/Space/etc.) if X11 is unavailable.

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
- Callback architecture testing with `CallbackSimple.*` and `CallbackArchitecture.*` test suites
- Integration testing with real audio files for end-to-end validation

### Test Build Structure (non-obvious)
- `nigamp_tests` does **not** link the app sources. Each test file `#include`s the implementation directly (e.g. `test_playlist.cpp` includes `../src/playlist.cpp`). A new source file under test must be included the same way, not added to the link.
- `test_audio_engine.cpp` (`AudioEngineTest.*`) is only compiled on Windows; there are no ALSA engine unit tests. `CallbackSimple`/`CallbackArchitecture` test mock engines against `include/audio_engine.hpp`.
- `MusicPlayer` lives entirely in `src/main.cpp` (no header), so its advancement/timing logic has no unit tests — validate it by running `nigamp` against `tests/data/*.mp3`.
- `test_hotkey_handler` and `test_music_player_simulation` are separate interactive executables, not registered with CTest.
- Project is C++17; Google Test 1.12.1 is fetched via FetchContent at configure time (needs network on first configure).

**Important**: Use `CallbackSimple.*` tests for callback mechanism validation - these avoid mutex deadlocks that can occur in complex mock implementations.

## Usage and Hotkeys

CLI flags (`--file/-f`, `--folder/-d`, `--preview/-p`, `--help`) and the full hotkey tables are in README.md. Default music directory: `C:\Music` on Windows, `~/Music` on Linux. Preview length (`PREVIEW_DURATION_SECONDS = 10`) and reindex interval (`REINDEX_INTERVAL_MINUTES = 10`) are constants in `MusicPlayer` in `src/main.cpp`.

## Third-Party Dependencies

All dependencies are header-only or statically linked:
- `minimp3.h` - MP3 decoding (header-only, included in repository)
- `dr_wav.h` - WAV decoding (header-only, downloaded by setup scripts)
- Google Test - Unit testing framework (fetched by CMake)
- Platform-specific system libraries:
  - **Windows**: DirectSound, User32, WinMM, OLE32, Shlwapi
  - **Linux**: ALSA (libasound2), X11 (optional, for global hotkeys)

## Debug Logging System

Logging macros are `#define`d locally at the top of individual `.cpp` files (currently `src/main.cpp` and `src/playlist.cpp`), not in a shared header — copy the block into a new file if it needs them:

```cpp
#define DEBUG_LOG(msg)  // Only active when compiled with -DDEBUG
#define INFO_LOG(msg)   // General information output
#define ERROR_LOG(msg)  // Error messages to stderr
```

- **Release Mode**: Only `INFO_LOG` and `ERROR_LOG` messages appear
- **Debug Mode**: All logging levels active for detailed debugging
- **Performance**: Zero overhead in release builds via macro elimination

## Critical Implementation Notes

### Enhanced Callback Architecture
The `CompletionCallback` mechanism includes dual verification methods:
- **Buffer Position Checking**:
  - Windows: DirectSound play cursor vs write position
  - Linux: `snd_pcm_avail()` (must be called to sync the hw pointer — on PipeWire/Pulse plugin devices `snd_pcm_state()`/`snd_pcm_delay()` alone never show the drain), then PCM state and delay
- **Time-Based Completion**: Calculates remaining playback time based on buffer size and sample rate
- **Structured Results**: `CompletionResult` provides detailed completion information with timing metrics
- **Error Handling**: Comprehensive error codes with descriptive messages

### Platform-Specific Implementation Notes

**Windows (DirectSound):**
- Uses secondary buffer with `DSBCAPS_GLOBALFOCUS` for background playback
- Circular buffer write cursor management with position-based completion detection
- Registers global hotkeys via `RegisterHotKey` Windows API
- Static linking with `-static` flags for standalone .exe deployment

**Linux (ALSA + X11):**
- ALSA PCM handle with period-based buffering for low-latency playback
- Global hotkeys via X11 `XGrabKey` with automatic fallback to terminal input
- Terminal input uses raw mode (`termios`) with non-blocking character reading
- Dynamic linking to system libraries (libasound2, libx11)
- Gracefully degrades when X11 is unavailable (headless/Wayland environments)

### Threading Safety Patterns
- Use atomic flags for cross-thread communication (`m_advance_to_next`, `m_should_quit`)
- Single mutex per component to avoid deadlocks (never nest mutex locks)
- Callback firing uses atomic exchange patterns to prevent double-execution
- Never sleep while holding a mutex in a loop that immediately re-locks it (e.g. the ALSA thread's `buffer_mutex`): `std::mutex` is unfair and the other thread (`write_samples`/`get_buffered_samples`) starves, which stalls playback entirely

### Mock Testing Guidelines
When creating mock implementations for testing:
- Use atomic variables instead of mutexes where possible
- Avoid nested lock acquisition in callback paths
- Use `SimpleMockEngine` pattern for deadlock-free testing
- Implement completion checking without holding multiple locks simultaneously
- Test completion callbacks with the `CallbackSimple.*` suites, which are designed to avoid deadlocks

### Development Workflow Notes
- **Duration Testing**: Use short test audio files (1-2 seconds) to verify completion timing
- **Countdown Display**: Test with both normal and preview modes to ensure proper formatting
- **Debug Output**: Enable DEBUG_LOG during development to trace timing and completion logic
- **Thread Safety**: Pay special attention to duration tracking variables in multi-threaded contexts

## Critical Bug Prevention

### Track Advancement Cascade Prevention
Recent fixes have resolved critical race conditions in track advancement. When modifying advancement logic, be aware of these patterns:

**Advancement Rules:**
- Set `m_advance_to_next` only via `request_advance()`, which checks `!m_stop_playback`
- Manual track changes (hotkeys) must not trigger an extra automatic advance

**State Reset Requirements** (`stop_current_song()`):
- Set `m_stop_playback`, join the playback thread, then `m_audio_engine->stop()` (joins the engine thread)
- Clear `m_advance_to_next` *after* both threads are stopped — either could have requested an advance during the stop
- `m_current_song_duration = 0.0` - clear cached duration
- **Preserve** `m_is_paused` state across track changes
- Clear audio engine completion callbacks to prevent stale callbacks

### Common Threading Pitfalls
- Completion callbacks fire on the engine thread and can race with manual stop signals
- Component `shutdown()` methods run twice (explicitly and from the destructor), so they must null out handles they release
- Background threads must poll `m_should_quit` in short slices; long sleeps delay process exit
- Always use atomic exchange patterns: `if (flag.exchange(false))` for single-execution semantics

## Code Quality and Development Standards

### Debug Logging Guidelines
The codebase uses a clean conditional compilation system for debug output:
- Use `DEBUG_LOG()`, `INFO_LOG()`, and `ERROR_LOG()` rather than raw `std::cout` for diagnostics (user-facing UI like the countdown line and `--help` text uses `std::cout` directly)
- Debug statements are automatically disabled in release builds via `#ifdef DEBUG`
- Keep production code clean by removing temporary debug statements after development
- Use `INFO_LOG()` for user-facing information and `ERROR_LOG()` for error reporting

### Build System Integration
The project uses CMake with platform detection:
- **Windows**: Automatically detects MinGW compiler paths, enables static linking for standalone deployment
- **Linux**: Uses system GCC/G++ compiler, links ALSA and optionally X11
- Required third-party headers are validated during CMake configuration
- Platform-specific source files selected automatically (audio_engine.cpp vs alsa_audio_engine.cpp, etc.)
- Test infrastructure is integrated with CTest for automated validation

### File Organization
- Headers in `include/` directory use `#pragma once` for include guards
- Implementation files in `src/` directory match header names
- Third-party dependencies in `third_party/` directory (header-only libraries)
- Test files in `tests/` directory with comprehensive coverage
- Build artifacts in `build/` directory (auto-generated)

