# Nigamp - Ultra-Lightweight Command-Line MP3 Player

A minimal-memory, high-quality command-line audio player for Windows that targets <10MB RAM usage while delivering audiophile-grade sound quality.

## Features

- **Ultra-lightweight**: <10MB RAM usage target
- **High-quality audio**: DirectSound-based audio engine for audiophile output
- **Global hotkeys**: Control playback without focus using Ctrl+Alt combinations
- **Shuffle mode**: Fisher-Yates shuffle algorithm for truly random playback
- **Multi-format support**: MP3 and WAV files
- **TDD-tested**: Comprehensive unit test coverage
- **Zero dependencies**: Single executable with static linking

## Controls

- **Ctrl+Alt+Right**: Next track
- **Ctrl+Alt+Left**: Previous track  
- **Ctrl+Alt+Space**: Pause/Resume
- **Ctrl+Alt+Up**: Volume up
- **Ctrl+Alt+Down**: Volume down
- **Ctrl+Alt+Escape**: Quit

## Building

### Prerequisites

1. **MinGW-w64** with GCC
2. **CMake** 3.16 or higher
3. **Git** (for downloading Google Test)

### Required Libraries

Download these libraries and place them in the `third_party/` directory:

1. **dr_wav.h** - Download from: https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h
2. **libmpg123** - Download Windows binaries from: https://www.mpg123.de/download.shtml
   - Extract and place headers in `third_party/mpg123/`
   - Place library files in `third_party/mpg123/lib/`

### Build Commands

```bash
# Configure
cmake -B build -G "MinGW Makefiles"

# Build
cmake --build build

# Run tests
ctest --test-dir build
```

### VSCode Integration

The project includes full VSCode integration with:
- IntelliSense configuration
- Build and debug tasks
- CMake integration
- Test runner

## Usage

1. Place MP3/WAV files in a directory
2. Navigate to that directory in command prompt
3. Run `nigamp.exe`
4. Use global hotkeys to control playback

The player will automatically:
- Scan the current directory for supported audio files
- Create a shuffled playlist
- Start playing the first track
- Continue playing through the entire playlist

## Architecture

### Core Components

- **AudioEngine**: DirectSound-based high-quality audio output
- **Decoder**: Pluggable MP3/WAV decoder system using libmpg123 and dr_wav
- **Playlist**: Fisher-Yates shuffle with navigation controls
- **HotkeyHandler**: Windows global hotkey registration and handling
- **FileScanner**: Directory scanning with format detection

### Design Principles

- **Dependency injection**: All components use interfaces for testability
- **RAII**: Proper resource management throughout
- **Thread-safe**: Concurrent audio processing and UI interaction
- **Memory efficient**: Minimal allocations, streaming audio processing

## Performance

- **Target RAM usage**: <10MB
- **Audio latency**: <50ms for hotkey responsiveness  
- **Supported formats**: MP3 (all variants), WAV
- **Audio quality**: Up to 48kHz/16-bit stereo

## License

MIT License - See LICENSE file for details.