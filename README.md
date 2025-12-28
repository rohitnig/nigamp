# Nigamp - Ultra-Lightweight Command-Line MP3 Player

A minimal-memory, high-quality command-line audio player for Windows that targets <10MB RAM usage while delivering audiophile-grade sound quality with instant track transitions and smooth playback.

## Features

### Core Capabilities
- **Ultra-lightweight**: <10MB RAM usage target with streaming audio architecture
- **High-quality audio**: DirectSound-based audio engine with ~50ms latency for responsive control
- **Duration-based completion**: Precise track advancement based on actual song duration, not file read completion
- **Live countdown display**: Real-time countdown showing remaining time on the same console line
- **Instant track transitions**: Zero-lag switching between songs when using hotkeys
- **Smart playlist management**: Fisher-Yates shuffle algorithm for truly random playback
- **Multi-format support**: MP3 (via minimp3) and WAV (via dr_wav) with automatic format detection
- **Background directory monitoring**: Automatic playlist updates every 10 minutes
- **Preview mode**: Play only first 10 seconds of each song for quick browsing
- **Volume boost**: Built-in 50% volume enhancement for better audio quality
- **Zero dependencies**: Single executable with static linking

### Playback Features
- **Continuous playback**: Seamless progression through entire playlist
- **Bidirectional navigation**: Move forward and backward through tracks
- **Precision timing**: Songs advance exactly when audio playback completes, not when file decoding finishes
- **Smart completion**: Continues playing until full song duration is reached, padding with silence if needed
- **Pause/Resume**: Maintains accurate timing and countdown display even when paused
- **Smooth transitions**: No gaps or delays between track changes
- **Thread-safe operation**: Concurrent audio processing with UI responsiveness

### User Interface
- **Global hotkeys**: Control playback from anywhere in Windows (works when console not focused)
- **Local hotkeys**: Additional controls when console window is active
- **Live countdown timer**: Dynamic display showing song title, remaining time, and total duration
- **Visual status indicators**: 🎵 for playing, ⏸️ [PAUSED] for paused, [PREVIEW] for preview mode
- **Same-line updates**: Countdown refreshes on the same console line without scrolling
- **Command-line options**: Flexible file and folder loading with preview mode

## Controls

### Windows

**Global Hotkeys (Work Anywhere)**
- **Ctrl+Alt+N**: Next track
- **Ctrl+Alt+P**: Previous track
- **Ctrl+Alt+R**: Pause/Resume
- **Ctrl+Alt+Plus**: Volume up
- **Ctrl+Alt+Minus**: Volume down
- **Ctrl+Alt+Escape**: Quit

**Local Hotkeys (Console Focused)**
- **Ctrl+N**: Next track
- **Ctrl+P**: Previous track
- **Ctrl+R**: Pause/Resume
- **Ctrl+Plus**: Volume up
- **Ctrl+Minus**: Volume down
- **Ctrl+Escape**: Quit

### Ubuntu/Linux

**Global Hotkeys (Work Anywhere)** - Requires X11 (libx11-dev)
- **Ctrl+Alt+N**: Next track
- **Ctrl+Alt+P**: Previous track
- **Ctrl+Alt+R**: Pause/Resume
- **Ctrl+Alt+Plus**: Volume up
- **Ctrl+Alt+Minus**: Volume down
- **Ctrl+Alt+Escape**: Quit

**Terminal Hotkeys** (works when terminal has focus, fallback if X11 unavailable)
- **N/n**: Next track
- **P/p**: Previous track
- **Space/R/r**: Pause/Resume
- **+/-**: Volume up/down
- **Q/q/ESC**: Quit

Note: Global hotkeys require X11 and will automatically fall back to terminal input if X11 is not available.

## Usage

### Command Line Options

```bash
# Basic usage - scans default music directory
# Windows: C:\Music
# Linux: ~/Music (or current directory)
nigamp

# Play specific file
nigamp --file song.mp3
nigamp -f "/path/to/My Song.mp3"

# Play all files from directory  
nigamp --folder "/path/to/Music"
nigamp -d "/home/user/Music"

# Preview mode - play first 10 seconds of each song
nigamp --preview
nigamp -p

# Combine options
nigamp -f song.mp3 -p    # Preview single file
nigamp -d "/path/to/Music" -p  # Preview entire directory

# Help
nigamp --help
nigamp -h
```

### What the App Does

1. **Automatic Directory Scanning**: When you run nigamp without arguments, it automatically scans the default music directory (`C:\Music` on Windows, `~/Music` or current directory on Linux) for MP3 and WAV files
2. **Intelligent Playlist Creation**: Creates a shuffled playlist using the Fisher-Yates algorithm for truly random playback
3. **Duration-Based Playback**: 
   - Reads actual song duration from audio files for precise timing
   - Advances tracks only when full song duration completes, not when file reading finishes
   - Continues playing with silence padding if decoder finishes early
4. **Live Visual Feedback**:
   - Shows real-time countdown with remaining time in MM:SS format
   - Updates on the same console line without scrolling: `🎵 Song Title - Time remaining: 02:45 / 04:20`
   - Displays pause status with visual indicators: `⏸️ [PAUSED]` 
5. **Instant Playback Control**: 
   - Press global hotkeys from anywhere in Windows to control playback
   - No need to focus the console window - hotkeys work system-wide
   - Zero-delay track switching when you press next/previous
6. **Smart Memory Management**: Uses streaming audio with minimal buffering to stay under 10MB RAM
7. **Background Monitoring**: Automatically rescans your music directory every 10 minutes to pick up new files
8. **Volume Enhancement**: Applies 50% volume boost for better audio quality
9. **Format Detection**: Automatically detects MP3 vs WAV files and uses appropriate decoder
10. **Continuous Operation**: Plays through entire playlist, then loops back to beginning
11. **Preview Mode**: Perfect for quickly browsing large music collections - plays first 10 seconds of each song with countdown

### Typical Workflow

#### Windows
1. **Setup**: Place your MP3/WAV files in `C:\Music` (or specify different directory)
2. **Launch**: Run `nigamp` from command prompt
3. **Background Operation**: Minimize console - music continues playing
4. **Control**: Use `Ctrl+Alt+N` and `Ctrl+Alt+P` to skip tracks from any application
5. **Volume**: Use `Ctrl+Alt+Plus/Minus` to adjust volume on the fly
6. **Pause**: Use `Ctrl+Alt+R` to pause/resume without switching applications

#### Ubuntu/Linux
1. **Setup**: Place your MP3/WAV files in `~/Music` or any directory
2. **Launch**: Run `./build/nigamp` from terminal
3. **Control**: Use `Ctrl+Alt+N` and `Ctrl+Alt+P` to skip tracks from any application (global hotkeys)
4. **Volume**: Use `Ctrl+Alt+Plus/Minus` to adjust volume on the fly
5. **Pause**: Use `Ctrl+Alt+R` to pause/resume without switching applications
6. **Fallback**: If X11 is not available, use terminal hotkeys (`N`, `P`, `Space`, etc.) when terminal has focus

## Building

### Prerequisites

#### Windows
1. **MinGW-w64** with GCC
2. **CMake** 3.16 or higher
3. **Git** (for downloading Google Test)

#### Ubuntu/Linux
1. **GCC/G++** (build-essential package)
2. **CMake** 3.16 or higher
3. **ALSA development libraries** (libasound2-dev)
4. **X11 development libraries** (libx11-dev) - for global hotkeys
5. **Git** (for downloading Google Test)

### Required Libraries

The project uses header-only libraries that are included in the repository:

1. **minimp3.h** - Header-only MP3 decoder (included)
2. **dr_wav.h** - Header-only WAV decoder (run setup script to download)

### Build Commands

#### Windows

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

# Run specific tests
build\test_hotkey_handler.exe
build\test_music_player_simulation.exe
build\nigamp_tests.exe
```

#### Ubuntu/Linux

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install -y build-essential cmake libasound2-dev libx11-dev

# Setup dependencies (run once)
./setup_libraries.sh

# Standard build
./build.sh

# Manual CMake build
cmake -B build
cmake --build build

# Run tests
ctest --test-dir build --verbose

# Run specific tests
./build/test_hotkey_handler
./build/test_music_player_simulation
./build/nigamp_tests
```

### VSCode Integration

The project includes full VSCode integration with:
- IntelliSense configuration
- Build and debug tasks
- CMake integration
- Test runner

## Advanced Features

### Threading Architecture
- **Main Thread**: UI and hotkey handling
- **Playback Thread**: Audio decoding and DirectSound buffer management  
- **Reindexing Thread**: Background directory scanning every 10 minutes

### Audio Pipeline
The audio engine uses platform-specific APIs with circular buffering for minimal latency:
- **Windows**: `File → Decoder → AudioBuffer → DirectSound → Speakers`
- **Linux**: `File → Decoder → AudioBuffer → ALSA → Speakers`

### Memory Optimization
- Streaming audio processing (no full file loading)
- Minimal buffering with configurable buffer sizes
- RAII resource management throughout
- Smart pointer usage for automatic cleanup

## Architecture

### Core Components

- **AudioEngine** (`audio_engine.hpp/cpp`): 
  - Windows: DirectSound-based audio output with ~50ms latency target
  - Linux: ALSA-based audio output with ~50ms latency target
- **Decoder** (`mp3_decoder.hpp/cpp`): Pluggable MP3/WAV decoder using minimp3 and dr_wav  
- **Playlist** (`playlist.hpp/cpp`): Fisher-Yates shuffle algorithm with bidirectional navigation
- **HotkeyHandler** (`hotkey_handler.hpp/cpp`): 
  - Windows: Global hotkey system using RegisterHotKey API
  - Linux: Terminal-based input handler
- **FileScanner** (`file_scanner.hpp/cpp`): Directory scanning with MP3/WAV format detection
- **MusicPlayer** (`main.cpp`): Main application orchestrating all components

### Design Principles

- **Interface Segregation**: All core components implement abstract interfaces (`I*` classes)
- **Dependency Injection**: All components use interfaces for testability and modularity
- **RAII**: Proper resource management with automatic cleanup
- **Thread Safety**: Concurrent audio processing with mutex-protected playlist operations
- **Streaming Architecture**: Minimal memory allocations, audio data streamed in chunks
- **Atomic Operations**: Lock-free communication between threads using atomic flags

## Performance

- **Target RAM usage**: <10MB with streaming architecture
- **Audio latency**: ~50ms for responsive hotkey control
- **Track switching**: Instant transitions with zero-delay  
- **Supported formats**: MP3 (via minimp3), WAV (via dr_wav)
- **Audio quality**: Up to 48kHz/16-bit stereo with 50% volume boost
- **Directory scanning**: Background updates every 10 minutes
- **Threading**: Optimized for concurrent audio processing and UI responsiveness

## Testing

The project uses comprehensive unit testing with Google Test:

```bash
# Run all tests
ctest --test-dir build --verbose

# Individual test executables
build\test_hotkey_handler.exe      # Interactive hotkey testing
build\test_music_player_simulation.exe  # Music player simulation
build\nigamp_tests.exe             # Core unit tests

# Test scripts
test_hotkeys.bat
test_music_simulation.bat
```

### Test Coverage
- Component isolation through dependency injection
- Mock implementations for audio and hotkey testing
- Separate test executables for interactive components
- TDD approach with test-first development

## License

MIT License - See LICENSE file for details.