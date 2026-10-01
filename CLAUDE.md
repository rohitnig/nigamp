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
- libx11-dev (X11 for Ctrl+Alt global hotkeys)
- libsystemd-dev (sd-bus for MPRIS media keys; optional, CMake falls back to a no-op)
- libfltk1.4-dev (`--gui` mini-player; optional, CMake falls back to a stub that reports no GUI support)
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
```

**Linux:**
```bash
# Install system dependencies
sudo apt-get update
sudo apt-get install -y build-essential cmake libasound2-dev libx11-dev libsystemd-dev libfltk1.4-dev git

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

# Run a subset via CTest (gtest_discover_tests registers each test case)
ctest --test-dir build -R Playlist

# Run specific test suites
./build/tests/nigamp_tests --gtest_filter="CallbackSimple.*"
./build/tests/nigamp_tests --gtest_filter="CallbackArchitecture.*"

# Manual test executables
./build/test_hotkey_handler
./build/test_music_player_simulation

# Integration testing (tests/data has test1.mp3 … test5.mp3)
./build/nigamp --file "tests/data/test1.mp3" --preview
```

`TESTING_CALLBACKS.md` lists the console messages that indicate completion detection is working (its commands are Windows-style).

## Architecture Overview

The application follows a dependency injection pattern: core components implement abstract `I*` interfaces, with platform-specific implementations selected by CMake.

### Core Components
- **AudioEngine** (`include/audio_engine.hpp`) - Platform-specific audio output
  - Windows: `src/audio_engine.cpp` (DirectSound-based)
  - Linux: `src/alsa_audio_engine.cpp` (ALSA-based)
- **Decoder** (`include/mp3_decoder.hpp`, `src/mp3_decoder.cpp`) - Pluggable MP3/WAV decoder using minimp3 and dr_wav
- **Playlist** (`include/playlist.hpp`, `src/playlist.cpp`) - Fisher-Yates shuffle algorithm with bidirectional navigation
- **HotkeyHandler** (`include/hotkey_handler.hpp`) - Platform-specific hotkey system
  - Windows: `src/hotkey_handler.cpp` (RegisterHotKey API)
  - Linux: `src/linux_hotkey_handler.cpp` (X11 global hotkeys + terminal fallback)
- **MediaControls** (`include/media_controls.hpp`) - Desktop media-key integration; commands arrive through the same `HotkeyCallback` as hotkeys, and `MusicPlayer` pushes track/status/volume to it
  - Linux: `src/mpris_media_controls.cpp` (MPRIS over sd-bus) when libsystemd is found
  - Windows / no libsystemd: `src/null_media_controls.cpp`
- **PlayerGui** (`include/player_gui.hpp`) - Optional `--gui` mini-player; extends `IMediaControls`, so it gets the same pushes (via `MusicPlayer::notify_media_views`) plus `set_position()` and an absolute-volume callback
  - `src/fltk_player_gui.cpp` (FLTK 1.4, fully custom-drawn in one `Fl_Double_Window`) when FLTK is found; else `src/null_player_gui.cpp` (`create_player_gui()` returns nullptr)
- **FileScanner** (`include/file_scanner.hpp`, `src/file_scanner.cpp`) - Directory scanning with MP3/WAV format detection
- **MusicPlayer** (`src/main.cpp`) - Main application orchestrating all components

### Threading Model
- Main thread: UI updates and hotkey handling; with `--gui` it runs the FLTK event loop (`IPlayerGui::pump()` replaces the 100ms sleep), so GUI commands execute on the main thread
- Playback thread (`MusicPlayer::playback_loop`): throttled decoding, countdown display, preview limit, completion safety timeout
- Audio engine thread: Platform-specific buffer management (DirectSound/ALSA) and completion callback firing
- Reindexing thread: Background directory scanning every 10 minutes

## Key Implementation Details

### Track Completion (EOF + device drain)
A track ends when the decoder hits EOF **and** the audio device has played everything queued — not when the song's nominal duration elapses (MP3 durations are bitrate estimates).

1. **Backpressure**: `playback_loop` decodes ~100ms chunks (`DECODE_CHUNK_MS`) only while `get_buffered_samples()` is below ~500ms (`MAX_QUEUED_AUDIO_MS`). Without this the decoder races ahead, the whole song lands in RAM, and EOF arrives seconds before the audio does.
2. **EOF**: when `decoder->is_eof()`, the loop calls `signal_eof()` on the engine.
3. **Drain**: the engine fires `CompletionCallback` (with a `CompletionResult`) once its pending queue is empty and the device has drained. The callback calls `request_advance()`.
   - DirectSound: play cursor vs write position.
   - ALSA: `snd_pcm_avail()` reports a full buffer or `-EPIPE`. `snd_pcm_avail()` must be called to sync the hw pointer — on PipeWire/Pulse plugin devices `snd_pcm_state()`/`snd_pcm_delay()` alone never show the drain.
4. **Safety net**: if no callback arrives within `COMPLETION_TIMEOUT_SECONDS` (3s) of *playing* time after EOF (pauses excluded), the loop forces an advance. Seeing the timeout message in normal playback means completion detection is broken.

Preview mode stops after 10s of playing time (pauses excluded) and advances directly. `get_duration()` is used only for the countdown display.

### Live Countdown Display
- Refreshes every 500ms on the same console line using `\r`; shows remaining/total as `MM:SS`
- Status indicators: 🎵 playing, ⏸️ [PAUSED], [PREVIEW]; the line is cleared when a song finishes
- Elapsed time excludes pauses; for MP3 the total is an estimate, so the countdown may not land exactly on 00:00

### Audio Latency
- **Windows**: File → Decoder → AudioBuffer → DirectSound secondary buffer (`DSBCAPS_GLOBALFOCUS` for background playback)
- **Linux**: File → Decoder → AudioBuffer → ALSA PCM buffer

The ALSA device buffer is 500ms with 50ms periods, and at most ~500ms more is queued in the engine's `pending_samples`. So stop/pause latency is bounded (~1s worst case) but not low — 50ms is the period size, not the end-to-end latency.

### Hotkey System
**Windows**: Global hotkeys (Ctrl+Alt+*) and local console hotkeys (Ctrl+*) using RegisterHotKey API.

**Linux**: Three input paths, all feeding `handle_hotkey()`:
- **MPRIS** (media keys, GNOME media menu, `playerctl`): the only global control that works on Wayland. sd-bus objects are single-threaded, so only the bus thread touches `sd_bus*`; other threads update state under `state_mutex` and set `dirty`, and the bus thread emits `PropertiesChanged`.
- **X11 Ctrl+Alt grabs** on the root window. Under Wayland (Ubuntu 26.04/GNOME 50 is Wayland-only) they only fire while an XWayland window has focus, and XTEST-injected keys go to the compositor, so test them on a nested X server: `Xephyr :99 -ac & DISPLAY=:99 xdotool key ctrl+alt+n`. Never inject keys on the user's live session.
  - `XGrabKey` always returns 1 — grab conflicts (`BadAccess`) only arrive via the X error handler after `XSync`
  - Grabs match the exact modifier state, so each key is grabbed with every CapsLock/NumLock combination; "Plus" is also grabbed with Shift (it's Shift+= on US layouts)
  - Ctrl+Alt+keypad +/- are reserved by the X server (`XF86Next/Prev_VMode`) and can't be grabbed
- **Terminal keys** (N/P/Space/Q) when the terminal has focus, read in raw `termios` mode with non-blocking reads.

### Mini-Player GUI (FLTK)
- Setters (`set_track`, `set_position`, ...) may be called from any thread and only update state under the GUI's mutex; all FLTK calls stay on the main thread inside `pump()`. Never invoke a command callback while holding that mutex — commands like next-track join the playback thread, which calls `set_position()`.
- Progress redraws are throttled (`PROGRESS_REDRAW_INTERVAL`). Each redraw generates X events that wake `Fl::wait()`, so redrawing on every pump becomes a busy loop (~13k draws/s) that also starves the X server.
- GNOME assigns a window with no matching `.desktop` entry to the app owning the process's cgroup — launched from a terminal, the GUI becomes one of the terminal's windows (no own Alt+Tab entry, terminal's dock icon). `install_desktop_entry.sh` installs `~/.local/share/applications/nigamp.desktop` with `StartupWMClass=nigamp` (matching `xclass("nigamp")`) and `resources/nigamp.png`. That PNG and `make_app_icon()` (the `_NET_WM_ICON`) draw the same design; keep them in sync.
- Two layouts in one window: full (440x116) and a compact bar (34px high, `border(0)`, always on top), toggled by double-clicking empty space or `C`. The window title bar belongs to the window manager (GNOME maximizes on its double-click), so the toggle can't live there. Mode/position/width/pin persist in `~/.config/nigamp/gui.conf` (`load_settings`/`save_settings`; saved on shutdown). Test runs should set `XDG_CONFIG_HOME` to a scratch dir so they don't overwrite the user's file.
- Without a window manager (bare Xephyr) keyboard focus follows the pointer, so `xdotool key` only reaches the window while the pointer is over it.
- Never pass a zero-size shape to FLTK drawing (e.g. `fl_pie` with radius 0). Ubuntu's FLTK draws through Cairo, and a zero-size shape puts the window's Cairo context into a permanent error state: every later draw is silently dropped and the window freezes half-drawn. `fill_circle()` guards this; a 1–4px progress fill early in a long track used to trigger it.
- Test the GUI with long real tracks, not only the short `tests/data` files: state-dependent rendering bugs (like the one above) only show at specific progress values.
- The window has no child widgets, so keys arrive as `FL_SHORTCUT`, not `FL_KEYBOARD`; Escape and the close button reach the window callback, which sends QUIT.
- On Linux, CMake uses `fltk-config` rather than `find_package(FLTK)`: Ubuntu's `FLTKConfig.cmake` contains literal `${DEB_HOST_MULTIARCH}` paths and fails at generate time. Windows fetches FLTK 1.4.4 via FetchContent and links it statically (untested).
- The GUI sets `FLTK_BACKEND=x11` (unless already set) whenever `DISPLAY` exists, so on GNOME it runs under XWayland. Native Wayland was a poor fit: GNOME offers Wayland clients no always-on-top, and the libdecor-cairo client-side title bar made the window hard to find. Stay-on-top uses `_NET_WM_STATE_ABOVE` (X11) / `HWND_TOPMOST` (Windows); the pin button is hidden where neither is available.
- FLTK 1.4 on its own prefers Wayland and connects to the default `wayland-0` socket even with `WAYLAND_DISPLAY` unset — so standalone FLTK test programs need `FLTK_BACKEND=x11` explicitly. To test on a nested server without touching the user's desktop: `DISPLAY=:99 ./build/nigamp --gui` with `WAYLAND_DISPLAY` unset, plus `DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent` to keep it off MPRIS and an `ALSA_CONFIG_PATH` file containing only `pcm.!default { type null }` to keep it silent (the null device drains instantly, so tracks advance in under a second).

### Memory Management
- `Mp3Decoder` reads the file through a fixed 64KB window (`Impl::fill()`/`next_frame()`); never load whole files — a 44MB MP3 used to cost 44MB of RSS. Measured: ~12.5MB total RSS / ~2.2MB private regardless of file size; the rest is shared libraries (libc, libstdc++, ALSA, PipeWire, libsystemd, X11).
- ID3v2 tags are skipped by reading their header size (they can hold MBs of album art) and excluded from the bitrate-based duration estimate.
- To verify decoder changes, decode real files with the old and new decoder and compare PCM checksums — output must be byte-identical.

## Testing

### Test Build Structure (non-obvious)
- `nigamp_tests` does **not** link the app sources. Each test file `#include`s the implementation directly (e.g. `test_playlist.cpp` includes `../src/playlist.cpp`). A new source file under test must be included the same way, not added to the link.
- `test_audio_engine.cpp` (`AudioEngineTest.*`) is only compiled on Windows; there are no ALSA engine unit tests. `CallbackSimple`/`CallbackArchitecture` test mock engines against `include/audio_engine.hpp`.
- `MusicPlayer` lives entirely in `src/main.cpp` (no header), so its advancement/timing logic has no unit tests — validate it by running `nigamp` against `tests/data/*.mp3`.
- `test_hotkey_handler` and `test_music_player_simulation` are separate interactive executables, not registered with CTest.
- Project is C++17; Google Test 1.12.1 is fetched via FetchContent at configure time (needs network on first configure).

### Mock Engines
Complex mocks with mutexes have deadlocked in callback paths. Follow the `SimpleMockEngine` pattern used by the `CallbackSimple.*` suite: atomics instead of mutexes where possible, never hold more than one lock while checking completion or firing callbacks.

## Usage and Hotkeys

CLI flags (`--file/-f`, `--folder/-d`, `--preview/-p`, `--gui/-g`, `--help`) and the full hotkey tables are in README.md. Default music directory: `C:\Music` on Windows, `~/Music` on Linux. Preview length (`PREVIEW_DURATION_SECONDS = 10`) and reindex interval (`REINDEX_INTERVAL_MINUTES = 10`) are constants in `MusicPlayer` in `src/main.cpp`.

## Third-Party Dependencies

- `minimp3.h` - MP3 decoding (header-only, included in repository)
- `dr_wav.h` - WAV decoding (header-only, downloaded by setup scripts)
- Google Test - Unit testing framework (fetched by CMake)
- Platform-specific system libraries:
  - **Windows**: DirectSound, User32, WinMM, OLE32, Shlwapi — linked with `-static` for a standalone .exe
  - **Linux**: ALSA (libasound2), X11 (optional, for global hotkeys), libsystemd (optional, for MPRIS), FLTK 1.4 (optional, for `--gui`; pulls in Pango/Cairo/Wayland) — dynamically linked; the app degrades gracefully without X11

## Debug Logging

Logging macros are `#define`d locally at the top of individual `.cpp` files (currently `src/main.cpp` and `src/playlist.cpp`), not in a shared header — copy the block into a new file if it needs them:

```cpp
#define DEBUG_LOG(msg)  // Only active when compiled with -DDEBUG
#define INFO_LOG(msg)   // General information output
#define ERROR_LOG(msg)  // Error messages to stderr
```

Use these rather than raw `std::cout` for diagnostics; user-facing UI (the countdown line, `--help` text) uses `std::cout` directly.

## Threading Rules and Bug Prevention

### Threading Safety Patterns
- Use atomic flags for cross-thread communication (`m_advance_to_next`, `m_should_quit`)
- Single mutex per component to avoid deadlocks (never nest mutex locks)
- Use atomic exchange for single-execution semantics: `if (flag.exchange(false))`
- Never sleep while holding a mutex in a loop that immediately re-locks it (e.g. the ALSA thread's `buffer_mutex`): `std::mutex` is unfair and the other thread (`write_samples`/`get_buffered_samples`) starves, which stalls playback entirely

### Track Advancement
- Set `m_advance_to_next` only via `request_advance()`, which checks `!m_stop_playback`
- Manual track changes (hotkeys) must not trigger an extra automatic advance
- Completion callbacks fire on the engine thread and can race with manual stop signals

**State Reset Requirements** (`stop_current_song()`):
- Set `m_stop_playback`, join the playback thread, then `m_audio_engine->stop()` (joins the engine thread)
- Clear `m_advance_to_next` *after* both threads are stopped — either could have requested an advance during the stop
- `m_current_song_duration = 0.0` - clear cached duration
- **Preserve** `m_is_paused` state across track changes
- Clear audio engine completion callbacks to prevent stale callbacks

### Lifecycle Pitfalls
- Component `shutdown()` methods run twice (explicitly and from the destructor), so they must null out handles they release
- Background threads must poll `m_should_quit` in short slices; long sleeps delay process exit
