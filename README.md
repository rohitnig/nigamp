# Nigamp - Ultra-Lightweight Command-Line MP3 Player

A minimal-memory, high-quality command-line audio player for Windows that targets <10MB RAM usage while delivering audiophile-grade sound quality with instant track transitions and smooth playback.

## Features

### Core Capabilities
- **Ultra-lightweight**: <10MB RAM usage target with streaming audio architecture
- **High-quality audio**: DirectSound-based audio engine with ~50ms latency for responsive control
- **Instant track transitions**: Zero-lag switching between songs when using hotkeys
- **Smart playlist management**: Fisher-Yates shuffle algorithm for truly random playback
- **Multi-format support**: MP3 (via minimp3) and WAV (via dr_wav) with automatic format detection
- **Background directory monitoring**: Automatic playlist updates every 10 minutes
- **Preview mode**: Play only first 10 seconds of each song for quick browsing
- **Volume boost**: Built-in 50% volume enhancement for better audio quality
- **TDD-tested**: Comprehensive unit test coverage with dependency injection
- **Zero dependencies**: Single executable with static linking

### Playback Features
- **Continuous playback**: Seamless progression through entire playlist
- **Bidirectional navigation**: Move forward and backward through tracks
- **Auto-advancement**: Songs automatically advance when completed
- **Smooth transitions**: No gaps or delays between track changes
- **Thread-safe operation**: Concurrent audio processing with UI responsiveness

### User Interface
- **Global hotkeys**: Control playback from anywhere in Windows (works when console not focused)
- **Local hotkeys**: Additional controls when console window is active
- **Real-time feedback**: Console output showing current track, volume, and playback status
- **Command-line options**: Flexible file and folder loading with preview mode

## Controls

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

## Usage

### Command Line Options

```bash
# Basic usage - scans C:\Music directory
nigamp

# Play specific file
nigamp --file song.mp3
nigamp -f "C:\Music\My Song.mp3"

# Play all files from directory  
nigamp --folder "C:\Music"
nigamp -d "C:\My Music Collection"

# Preview mode - play first 10 seconds of each song
nigamp --preview
nigamp -p

# Combine options
nigamp -f song.mp3 -p    # Preview single file
nigamp -d "C:\Music" -p  # Preview entire directory

# Help
nigamp --help
nigamp -h
```

### What the App Does

1. **Automatic Directory Scanning**: When you run nigamp without arguments, it automatically scans `C:\Music` for MP3 and WAV files
2. **Intelligent Playlist Creation**: Creates a shuffled playlist using the Fisher-Yates algorithm for truly random playback
3. **Instant Playback Control**: 
   - Press global hotkeys from anywhere in Windows to control playback
   - No need to focus the console window - hotkeys work system-wide
   - Zero-delay track switching when you press next/previous
4. **Smart Memory Management**: Uses streaming audio with minimal buffering to stay under 10MB RAM
5. **Background Monitoring**: Automatically rescans your music directory every 10 minutes to pick up new files
6. **Volume Enhancement**: Applies 50% volume boost for better audio quality
7. **Format Detection**: Automatically detects MP3 vs WAV files and uses appropriate decoder
8. **Continuous Operation**: Plays through entire playlist, then loops back to beginning
9. **Preview Mode**: Perfect for quickly browsing large music collections - plays first 10 seconds of each song

### Typical Workflow

1. **Setup**: Place your MP3/WAV files in `C:\Music` (or specify different directory)
2. **Launch**: Run `nigamp` from command prompt
3. **Background Operation**: Minimize console - music continues playing
4. **Control**: Use `Ctrl+Alt+N` and `Ctrl+Alt+P` to skip tracks from any application
5. **Volume**: Use `Ctrl+Alt+Plus/Minus` to adjust volume on the fly
6. **Pause**: Use `Ctrl+Alt+R` to pause/resume without switching applications

## Building

### Prerequisites

1. **MinGW-w64** with GCC
2. **CMake** 3.16 or higher
3. **Git** (for downloading Google Test)

### Required Libraries

The project uses header-only libraries that are included in the repository:

1. **minimp3.h** - Header-only MP3 decoder (included)
2. **dr_wav.h** - Header-only WAV decoder (run `copy_drwav.bat` to download)

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

# Run specific tests
build\test_hotkey_handler.exe
build\test_music_player_simulation.exe
build\nigamp_tests.exe
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
The audio engine uses DirectSound with circular buffering for minimal latency:
```
File → Decoder → AudioBuffer → DirectSound → Speakers
```

### Memory Optimization
- Streaming audio processing (no full file loading)
- Minimal buffering with configurable buffer sizes
- RAII resource management throughout
- Smart pointer usage for automatic cleanup

## Architecture

### Core Components

- **AudioEngine** (`audio_engine.hpp/cpp`): DirectSound-based audio output with ~50ms latency target
- **Decoder** (`mp3_decoder.hpp/cpp`): Pluggable MP3/WAV decoder using minimp3 and dr_wav  
- **Playlist** (`playlist.hpp/cpp`): Fisher-Yates shuffle algorithm with bidirectional navigation
- **HotkeyHandler** (`hotkey_handler.hpp/cpp`): Windows global hotkey system using RegisterHotKey API
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