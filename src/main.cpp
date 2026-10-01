#include "audio_engine.hpp"
#include "mp3_decoder.hpp"
#include "playlist.hpp"
#include "hotkey_handler.hpp"
#include "file_scanner.hpp"
#include "media_controls.hpp"
#include "player_gui.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <mutex>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
#elif __linux__
    #include <unistd.h>
    #include <pwd.h>
#endif

// Simple debug logging
#ifdef DEBUG
    #define DEBUG_LOG(msg) std::cout << "[DEBUG] " << msg << std::endl
#else
    #define DEBUG_LOG(msg) do {} while(0)
#endif

#define INFO_LOG(msg) std::cout << "[INFO] " << msg << std::endl
#define ERROR_LOG(msg) std::cerr << "[ERROR] " << msg << std::endl

namespace nigamp {

// Get platform-specific default music directory
std::string get_default_music_directory() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_MYMUSIC, nullptr, SHGFP_TYPE_CURRENT, path))) {
        return std::string(path);
    }
    return "C:\\Music";  // Fallback
#elif __linux__
    const char* home = getenv("HOME");
    if (home) {
        std::string music_dir = std::string(home) + "/Music";
        if (std::filesystem::exists(music_dir)) {
            return music_dir;
        }
    }
    return ".";  // Fallback to current directory
#else
    return ".";
#endif
}

class MusicPlayer {
private:
    std::unique_ptr<IAudioEngine> m_audio_engine;
    
    // Helper function to format time as MM:SS
    std::string format_time(double seconds) {
        const int total = std::max(0, static_cast<int>(seconds));
        const int minutes = total / 60;
        const int secs = total % 60;
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, secs);
        return std::string(buffer);
    }
    std::unique_ptr<IPlaylist> m_playlist;
    std::unique_ptr<IHotkeyHandler> m_hotkey_handler;
    std::unique_ptr<IMediaControls> m_media_controls;
    std::unique_ptr<IPlayerGui> m_gui;  // only with --gui
    std::unique_ptr<IFileScanner> m_file_scanner;
    std::unique_ptr<IAudioDecoder> m_current_decoder;
    
    std::atomic<bool> m_should_quit{false};
    std::atomic<bool> m_is_paused{false};
    std::atomic<bool> m_advance_to_next{false};
    std::atomic<bool> m_stop_playback{false};
    std::thread m_playback_thread;
    std::thread m_reindex_thread;
    std::mutex m_playlist_mutex;
    
    const Song* m_current_song = nullptr;
    float m_volume = DEFAULT_VOLUME;
    bool m_preview_mode = false;
    bool m_gui_requested = false;
    
    // Safety net if the engine never reports completion after decoder EOF
    // (counts playing time only, so pausing near the end does not trigger it)
    static constexpr int COMPLETION_TIMEOUT_SECONDS = 3;
    
    // Song length from the decoder; used for the countdown display only
    // (MP3 durations are bitrate estimates, so completion never depends on it)
    double m_current_song_duration = 0.0;
    
    // Decode in ~100ms chunks and keep at most ~500ms queued ahead of the audio device
    static constexpr int DECODE_CHUNK_MS = 100;
    static constexpr int MAX_QUEUED_AUDIO_MS = 500;
    
    // Constants
    static constexpr double DEFAULT_VOLUME = 0.8;
    static constexpr int COUNTDOWN_UPDATE_INTERVAL_MS = 500;
    static constexpr int PREVIEW_DURATION_SECONDS = 10;
    
    // Reindexing properties
    std::string m_current_directory = ".";
    std::chrono::steady_clock::time_point m_last_index_time;
    static constexpr int REINDEX_INTERVAL_MINUTES = 10;

public:
    MusicPlayer(bool preview_mode = false, bool gui = false)
        : m_preview_mode(preview_mode), m_gui_requested(gui) {
        m_audio_engine = create_audio_engine();
        m_playlist = create_playlist();
        m_hotkey_handler = create_hotkey_handler();
        m_media_controls = create_media_controls();
        m_file_scanner = create_file_scanner();
        m_last_index_time = std::chrono::steady_clock::now();
    }
    
    ~MusicPlayer() {
        shutdown();
    }
    
    bool initialize() {
        if (!m_hotkey_handler->initialize()) {
            std::cerr << "Failed to initialize hotkey handler\n";
            return false;
        }
        
        m_hotkey_handler->set_callback([this](HotkeyAction action) {
            handle_hotkey(action);
        });
        
        if (!m_hotkey_handler->register_hotkeys()) {
            std::cout << "Warning: Some global hotkeys are unavailable; the others still work\n";
        } else {
            std::cout << "Global hotkeys registered successfully!\n";
        }
        
        // Start the message loop immediately after hotkey registration
        m_hotkey_handler->process_messages();
        
        // Media keys and desktop media widgets (MPRIS on Linux); optional
        if (m_media_controls->initialize([this](HotkeyAction action) { handle_hotkey(action); })) {
            std::cout << "Media controls enabled (media keys, system media menu)\n";
            m_media_controls->set_volume(m_volume);
        }
        
        if (m_gui_requested && !initialize_gui()) {
            return false;
        }
        
        return true;
    }
    
    bool load_directory(const std::string& directory) {
        SongList songs = m_file_scanner->scan_directory(directory);
        
        if (songs.empty()) {
            std::cerr << "No supported audio files found in directory: " << directory << "\n";
            return false;
        }
        
        m_playlist->clear();
        for (const auto& song : songs) {
            m_playlist->add_song(song);
        }
        
        m_playlist->shuffle();
        
        std::cout << "Loaded " << songs.size() << " songs from " << directory << "\n";
        return true;
    }
    
    bool load_file(const std::string& file_path) {
        SongList songs = m_file_scanner->scan_directory(std::filesystem::path(file_path).parent_path().string());
        
        // Filter to only include the specified file
        auto it = std::find_if(songs.begin(), songs.end(), 
            [&file_path](const Song& song) {
                return std::filesystem::path(song.file_path) == std::filesystem::path(file_path);
            });
            
        if (it == songs.end()) {
            std::cerr << "File not found or not supported: " << file_path << "\n";
            return false;
        }
        
        m_playlist->clear();
        m_playlist->add_song(*it);
        
        std::cout << "Loaded file: " << file_path << "\n";
        return true;
    }
    
    void run(const std::string& path = "", bool is_file = false) {
        std::cout << "Nigamp - Ultra-Lightweight MP3 Player\n";
        std::cout << "======================================\n";
#ifdef _WIN32
        std::cout << "Global Hotkeys (work anywhere):\n";
        std::cout << "  Ctrl+Alt+N      - Next track\n";
        std::cout << "  Ctrl+Alt+P      - Previous track\n";
        std::cout << "  Ctrl+Alt+R      - Pause/Resume\n";
        std::cout << "  Ctrl+Alt+Plus   - Volume up\n";
        std::cout << "  Ctrl+Alt+Minus  - Volume down\n";
        std::cout << "  Ctrl+Alt+Escape - Quit\n";
        std::cout << "\n";
        std::cout << "Local Hotkeys (when console focused):\n";
        std::cout << "  Ctrl+N          - Next track\n";
        std::cout << "  Ctrl+P          - Previous track\n";
        std::cout << "  Ctrl+R          - Pause/Resume\n";
        std::cout << "  Ctrl+Plus       - Volume up\n";
        std::cout << "  Ctrl+Minus      - Volume down\n";
        std::cout << "  Ctrl+Escape     - Quit\n";
#else
        std::cout << "Global Hotkeys (work anywhere, if X11 available):\n";
        std::cout << "  Ctrl+Alt+N      - Next track\n";
        std::cout << "  Ctrl+Alt+P      - Previous track\n";
        std::cout << "  Ctrl+Alt+R      - Pause/Resume\n";
        std::cout << "  Ctrl+Alt+Plus   - Volume up\n";
        std::cout << "  Ctrl+Alt+Minus  - Volume down\n";
        std::cout << "  Ctrl+Alt+Escape - Quit\n";
        std::cout << "\nMedia Keys (work anywhere, including Wayland):\n";
        std::cout << "  Play/Pause, Next, Previous, and the system media menu\n";
        std::cout << "\nTerminal Hotkeys (when terminal has focus):\n";
        std::cout << "  N/n             - Next track\n";
        std::cout << "  P/p             - Previous track\n";
        std::cout << "  Space/R/r       - Pause/Resume\n";
        std::cout << "  +/-             - Volume up/down\n";
        std::cout << "  Q/q/ESC         - Quit\n";
#endif
        std::cout << "======================================\n\n";
        
        bool loaded = false;
        if (!path.empty()) {
            if (is_file) {
                loaded = load_file(path);
                // For single files, store parent directory for reindexing
                m_current_directory = std::filesystem::path(path).parent_path().string();
            } else {
                loaded = load_directory(path);
                m_current_directory = path;
            }
        } else {
            std::string default_dir = get_default_music_directory();
            loaded = load_directory(default_dir);
            m_current_directory = default_dir;
        }
        
        if (!loaded) {
            std::cerr << "Failed to load audio files\n";
            return;
        }
        
        // Start background reindexing thread
        start_reindexing_thread();
        
        play_current_song();
        
        while (!m_should_quit) {
            // Handle track advancement requests from playback thread
            if (m_advance_to_next.exchange(false)) {
                handle_track_advance();
            }
            
            if (m_gui) {
                m_gui->pump(0.1);  // window events; commands run on this thread
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
    
private:
    bool initialize_gui() {
        m_gui = create_player_gui();
        if (!m_gui) {
            std::cerr << "This build of nigamp has no GUI support (FLTK was not found at build time)\n";
            return false;
        }
        m_gui->set_volume_callback([this](double volume) { set_volume(static_cast<float>(volume)); });
        m_gui->set_preview_mode(m_preview_mode);
        m_gui->set_volume(m_volume);
        if (!m_gui->initialize([this](HotkeyAction action) { handle_hotkey(action); })) {
            std::cerr << "Failed to open the GUI window (no display available)\n";
            m_gui.reset();
            return false;
        }
        return true;
    }
    
    // Pushes player state to the desktop media controls and, with --gui, the window
    template <typename F>
    void notify_media_views(F&& update) {
        update(*m_media_controls);
        if (m_gui) {
            update(*m_gui);
        }
    }
    
    void handle_playback_completion(const CompletionResult& result) {
        if (result.error_code != AudioEngineError::SUCCESS) {
            ERROR_LOG("Audio playback completed with error: " << result.error_message);
        } else {
            DEBUG_LOG("Audio playback completed after " << result.completion_time.count() << "ms");
        }
        
        request_advance();
    }
    
    // Ask the main loop to move to the next track, unless a manual stop is in progress
    void request_advance() {
        if (!m_stop_playback.load()) {
            m_advance_to_next = true;
        }
    }
    
    void handle_hotkey(HotkeyAction action) {
        // Use a thread-safe queue to pass hotkey actions to the main thread
        // This ensures that all hotkey actions are processed in the main thread
        // and avoids any potential race conditions.
        
        // For now, we'll just process the hotkey directly, but in a more complex
        // application, a thread-safe queue would be a better approach.
        
        switch (action) {
            case HotkeyAction::NEXT_TRACK:
                next_track();
                break;
            case HotkeyAction::PREVIOUS_TRACK:
                previous_track();
                break;
            case HotkeyAction::PAUSE_RESUME:
                toggle_pause();
                break;
            case HotkeyAction::VOLUME_UP:
                adjust_volume(0.1f);
                break;
            case HotkeyAction::VOLUME_DOWN:
                adjust_volume(-0.1f);
                break;
            case HotkeyAction::QUIT:
                quit();
                break;
        }
    }
    
    void handle_track_advance() {
        std::lock_guard<std::mutex> lock(m_playlist_mutex);
        
        // For single-file preview mode, quit after completion instead of looping
        if (m_preview_mode && m_playlist->size() == 1) {
            std::cout << "Preview complete for single file. Exiting...\n";
            // Set quit flag instead of calling quit() directly to avoid deadlock
            m_should_quit = true;
            return;
        }
        
        // For automatic advancement after song completion, move to next song
        const Song* next_song = m_playlist->next();
        if (next_song && next_song != m_current_song) {
            std::cout << "Auto-advancing to next track: " << next_song->title << "\n";
            stop_current_song();
            m_current_song = next_song;
            play_current_song();
        } else if (next_song == m_current_song) {
            // Single song in playlist - for preview mode, quit; otherwise loop
            if (m_preview_mode) {
                std::cout << "Preview mode with single song complete. Exiting...\n";
                // Set quit flag instead of calling quit() directly to avoid deadlock
                m_should_quit = true;
            } else {
                std::cout << "Single song playlist - restarting current song\n";
                stop_current_song();
                play_current_song();
            }
        } else {
            std::cout << "No more tracks, staying on current song\n";
        }
    }
    
    void next_track() {
        std::lock_guard<std::mutex> lock(m_playlist_mutex);
        
        const Song* next_song = m_playlist->next();
        if (next_song) {
            stop_current_song();
            INFO_LOG("Now playing: " << next_song->title);
            m_current_song = next_song;
            play_current_song();
        }
        else {
            std::cout << "No next track available\n";
        }
    }
    
    void previous_track() {
        std::lock_guard<std::mutex> lock(m_playlist_mutex);
        const Song* prev_song = m_playlist->previous();
        if (prev_song) {
            stop_current_song();
            m_current_song = prev_song;
            play_current_song();
        }
    }
    
    void toggle_pause() {
        if (m_audio_engine->is_playing()) {
            m_audio_engine->pause();
            m_is_paused = true;
            notify_media_views([](IMediaControls& view) { view.set_playback_status(PlaybackStatus::PAUSED); });
            std::cout << "Paused\n";
        } else {
            m_audio_engine->resume();
            m_is_paused = false;
            notify_media_views([](IMediaControls& view) { view.set_playback_status(PlaybackStatus::PLAYING); });
            std::cout << "Resumed\n";
        }
    }
    
    void adjust_volume(float delta) {
        set_volume(m_volume + delta);
    }
    
    void set_volume(float volume) {
        m_volume = std::clamp(volume, 0.0f, 1.0f);
        m_audio_engine->set_volume(m_volume);
        notify_media_views([this](IMediaControls& view) { view.set_volume(m_volume); });
        std::cout << "Volume: " << static_cast<int>(m_volume * 100 + 0.5f) << "%\n";
    }
    
    void quit() {
        std::cout << "Shutting down...\n";
        m_should_quit = true;
    }
    
    void play_current_song() {
        if (!m_current_song) {
            m_current_song = m_playlist->current();
        }
        
        if (!m_current_song) {
            std::cout << "No songs to play\n";
            return;
        }
        
        if (m_preview_mode) {
            std::cout << "Now playing (10s preview): " << m_current_song->title << "\n";
        } else {
            std::cout << "Now playing: " << m_current_song->title << "\n";
        }
        
        m_current_decoder = create_decoder(m_current_song->file_path);
        if (!m_current_decoder || !m_current_decoder->open(m_current_song->file_path)) {
            ERROR_LOG("Failed to open: " << m_current_song->file_path);
            return;
        }
        
        m_current_song_duration = m_current_decoder->get_duration();
        
        AudioFormat format = m_current_decoder->get_format();
        if (!m_audio_engine->initialize(format)) {
            ERROR_LOG("Failed to initialize audio engine");
            return;
        }
        
        // Set up callback for track advancement
        m_audio_engine->set_completion_callback([this](const CompletionResult& result) {
            handle_playback_completion(result);
        });
        
        m_audio_engine->set_volume(m_volume);
        
        if (!m_audio_engine->start()) {
            ERROR_LOG("Failed to start audio engine");
            return;
        }
        
        const TrackInfo track{m_current_song->title, m_current_song->file_path, m_current_song_duration};
        const PlaybackStatus status = m_is_paused ? PlaybackStatus::PAUSED : PlaybackStatus::PLAYING;
        notify_media_views([&](IMediaControls& view) {
            view.set_track(track);
            view.set_playback_status(status);
        });
        
        m_playback_thread = std::thread(&MusicPlayer::playback_loop, this);
    }
    
    void stop_current_song() {
        
        m_advance_to_next = false;
        
        // Reset playback state controllers (but preserve pause state)
        m_current_song_duration = 0.0;
        // Note: m_is_paused is preserved so next song respects current pause state
        
        // Signal playback loop to exit FIRST - this prevents further mutex contention
        m_stop_playback = true;        
        
        // Wait for playback thread to finish. This is the most important change.
        // By joining here, we ensure the thread is no longer accessing the decoder or audio engine.
        if (m_playback_thread.joinable()) {
            std::cout << "Waiting for playback thread to finish...\n";
            m_playback_thread.join();
            std::cout << "Playback thread stopped\n";
        }
        
        // Now that the thread is stopped, it's safe to stop hardware and close resources.
        if (m_audio_engine) {
            std::cout << "Stopping audio engine...\n";
            m_audio_engine->stop();
        }
        
        if (m_current_decoder) {
            std::cout << "Force closing decoder for instant stop...\n";
            m_current_decoder->close();
        }
        
        // Both the playback thread and the engine thread are stopped now, so neither can
        // set the flag again; clear any advance they requested while we were stopping.
        m_advance_to_next = false;
        
        // Reset for next playback
        m_stop_playback = false;

        if (m_current_decoder) {
            std::cout << "Resetting decoder...\n";
            m_current_decoder.reset();
        }
        
        // Clear audio engine completion callback after all threads are stopped
        if (m_audio_engine) {
            m_audio_engine->set_completion_callback(nullptr);
        }
    }    

    void update_countdown_display(double played_seconds) {
        const char* status = m_is_paused ? "⏸️  [PAUSED]" : (m_preview_mode ? "🎵 [PREVIEW]" : "🎵");
        const double total = m_preview_mode ? PREVIEW_DURATION_SECONDS : m_current_song_duration;
        if (m_gui) {
            m_gui->set_position(played_seconds, total);
        }
        if (total <= 0) {
            return;
        }
        std::cout << "\r" << status << " " << (m_current_song ? m_current_song->title : "Unknown")
                  << " - Time remaining: " << format_time(total - played_seconds)
                  << " / " << format_time(total) << std::flush;
    }
    
    void clear_countdown_line() {
        std::cout << "\r" << std::string(80, ' ') << "\r" << std::flush;
    }
    
    void playback_loop() {
        try {
            using clock = std::chrono::steady_clock;
            
            AudioBuffer buffer;
            const AudioFormat format = m_current_decoder ? m_current_decoder->get_format() : AudioFormat{};
            const size_t channels = std::max<size_t>(1, format.channels);
            const size_t sample_rate = std::max<size_t>(1, format.sample_rate);
            const size_t chunk_samples = sample_rate * DECODE_CHUNK_MS / 1000 * channels;
            const size_t max_queued_samples = sample_rate * MAX_QUEUED_AUDIO_MS / 1000 * channels;
            
            const auto preview_duration = std::chrono::seconds(PREVIEW_DURATION_SECONDS);
            const auto completion_timeout = std::chrono::seconds(COMPLETION_TIMEOUT_SECONDS);
            const auto display_update_interval = std::chrono::milliseconds(COUNTDOWN_UPDATE_INTERVAL_MS);
            
            // Time actually spent playing (pauses excluded), measured from when this loop starts
            clock::duration played{0};
            clock::duration played_since_eof{0};
            bool eof_signaled = false;
            
            auto last_tick = clock::now();
            auto last_display_update = last_tick - display_update_interval;
            
            while (!m_stop_playback && !m_should_quit && m_current_decoder) {
                const auto now = clock::now();
                if (!m_is_paused) {
                    played += now - last_tick;
                    if (eof_signaled) {
                        played_since_eof += now - last_tick;
                    }
                }
                last_tick = now;
                
                if (now - last_display_update >= display_update_interval) {
                    update_countdown_display(std::chrono::duration<double>(played).count());
                    last_display_update = now;
                }
                
                if (m_preview_mode && played >= preview_duration) {
                    clear_countdown_line();
                    if (m_current_song) {
                        INFO_LOG("Preview complete for: " << m_current_song->title);
                    }
                    request_advance();
                    break;
                }
                
                if (!eof_signaled && !m_is_paused) {
                    // Backpressure: decode only while the engine is running low on audio
                    while (m_audio_engine->get_buffered_samples() < max_queued_samples &&
                           m_current_decoder->decode(buffer, chunk_samples)) {
                        m_audio_engine->write_samples(buffer);
                    }
                    
                    if (m_current_decoder->is_eof()) {
                        // The engine fires the completion callback once the device has drained
                        m_audio_engine->signal_eof();
                        eof_signaled = true;
                    }
                }
                
                if (eof_signaled && played_since_eof >= completion_timeout) {
                    clear_countdown_line();
                    ERROR_LOG("Audio completion callback timeout after " << COMPLETION_TIMEOUT_SECONDS
                              << " seconds. Forcing track advance.");
                    request_advance();
                    break;
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            clear_countdown_line();
            
        } catch (const std::exception& e) {
            std::cerr << "Exception in playback_loop: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception in playback_loop" << std::endl;
        }
    }
    
    void start_reindexing_thread() {
        if (!m_reindex_thread.joinable()) {
            m_reindex_thread = std::thread(&MusicPlayer::reindexing_loop, this);
        }
    }
    
    void reindexing_loop() {
        try {
            while (!m_should_quit) {
                // Check every minute, in short slices so quitting isn't delayed by up to a minute
                const auto wake = std::chrono::steady_clock::now() + std::chrono::minutes(1);
                while (!m_should_quit && std::chrono::steady_clock::now() < wake) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }

                if (m_should_quit) break;

                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - m_last_index_time);
                
                if (elapsed.count() >= REINDEX_INTERVAL_MINUTES) {
                    reindex_directory();
                    m_last_index_time = now;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception in reindexing_loop: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception in reindexing_loop" << std::endl;
        }
    }
    
    void reindex_directory() {
        if (m_current_directory.empty()) return;
        
        SongList new_songs = m_file_scanner->scan_directory(m_current_directory);
        
        {
            std::lock_guard<std::mutex> lock(m_playlist_mutex);
            
            // Get current playlist songs
            size_t playlist_size = m_playlist->size();
            if (playlist_size > 0) {
                // Simple approach: if song count changed, update playlist
                if (new_songs.size() != playlist_size) {
                    std::cout << "Directory updated: Found " << new_songs.size() 
                             << " songs (was " << playlist_size << ")\n";
                    
                    // Store current song info to try to maintain playback
                    std::string current_song_path;
                    if (m_current_song) {
                        current_song_path = m_current_song->file_path;
                    }
                    
                    // Update playlist
                    m_playlist->clear();
                    for (const auto& song : new_songs) {
                        m_playlist->add_song(song);
                    }
                    
                    if (!new_songs.empty()) {
                        m_playlist->shuffle();
                        
                        // Try to find the currently playing song in the new list
                        if (!current_song_path.empty()) {
                            // This is a simplified approach - in a full implementation
                            // we might want to maintain playback position
                            std::cout << "Playlist updated during playbook\n";
                        }
                    }
                }
            }
        }
    }
    
    void shutdown() {
        std::cout << "Shutting down music player...\n";
        
        // Signal all threads to stop
        m_should_quit = true;
        
        // Stop taking desktop media commands before tearing down playback
        if (m_media_controls) {
            m_media_controls->shutdown();
        }
        if (m_gui) {
            m_gui->shutdown();
        }
        
        // Stop audio playback first
        if (m_audio_engine) {
            m_audio_engine->stop();
        }
        
        // Wait for playback thread to finish
        if (m_playback_thread.joinable()) {
            std::cout << "Waiting for playback thread to finish...\n";
            m_playback_thread.join();
            std::cout << "Playback thread finished\n";
        }
        
        // Wait for reindexing thread to finish
        if (m_reindex_thread.joinable()) {
            std::cout << "Waiting for reindexing thread to finish...\n";
            m_reindex_thread.join();
            std::cout << "Reindexing thread finished\n";
        }
        
        // Clean up resources
        if (m_current_decoder) {
            m_current_decoder->close();
            m_current_decoder.reset();
        }
        
        if (m_hotkey_handler) {
            m_hotkey_handler->shutdown();
        }
        
        if (m_audio_engine) {
            m_audio_engine->shutdown();
        }
        
        std::cout << "Shutdown complete\n";
    }
};

}

int main(int argc, char* argv[]) {
    try {
        bool preview_mode = false;
        bool gui = false;
        std::string target_path = "";
        bool is_file = false;
        
        // Parse command line arguments
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            if (arg == "--gui" || arg == "-g") {
                gui = true;
            } else if (arg == "--preview" || arg == "-p") {
                preview_mode = true;
                std::cout << "Preview mode enabled: Playing 10 seconds per song\n";
            } else if (arg == "--file" || arg == "-f") {
                if (i + 1 < argc) {
                    target_path = argv[++i];
                    is_file = true;
                } else {
                    std::cerr << "Error: --file requires a file path\n";
                    return 1;
                }
            } else if (arg == "--folder" || arg == "-d") {
                if (i + 1 < argc) {
                    target_path = argv[++i];
                    is_file = false;
                } else {
                    std::cerr << "Error: --folder requires a directory path\n";
                    return 1;
                }
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: nigamp [options]\n";
                std::cout << "Options:\n";
                std::cout << "  --file <path>, -f <path>     Play specific MP3/WAV file\n";
                std::cout << "  --folder <path>, -d <path>   Play all files from directory\n";
                std::cout << "  --preview, -p                Play only first 10 seconds of each song\n";
                std::cout << "  --gui, -g                    Show the mini-player window\n";
                std::cout << "  --help, -h                   Show this help message\n";
                std::cout << "\nUsage Examples:\n";
#ifdef _WIN32
                std::cout << "  nigamp                       Scan default Music directory for MP3/WAV files\n";
                std::cout << "  nigamp --file song.mp3       Play single file\n";
                std::cout << "  nigamp --folder \"C:\\Music\"   Play all files from folder\n";
#else
                std::cout << "  nigamp                       Scan ~/Music or current directory for MP3/WAV files\n";
                std::cout << "  nigamp --file song.mp3       Play single file\n";
                std::cout << "  nigamp --folder \"/path/to/Music\"   Play all files from folder\n";
#endif
                std::cout << "  nigamp -f song.mp3 -p        Play single file in preview mode\n";
#ifdef _WIN32
                std::cout << "\nGlobal Hotkeys (work anywhere):\n";
                std::cout << "  Ctrl+Alt+N                   Next track\n";
                std::cout << "  Ctrl+Alt+P                   Previous track\n";
                std::cout << "  Ctrl+Alt+R                   Pause/Resume\n";
                std::cout << "  Ctrl+Alt+Plus/Minus          Volume control\n";
                std::cout << "  Ctrl+Alt+Escape              Quit\n";
                std::cout << "\nLocal Hotkeys (when console focused):\n";
                std::cout << "  Ctrl+N                       Next track\n";
                std::cout << "  Ctrl+P                       Previous track\n";
                std::cout << "  Ctrl+R                       Pause/Resume\n";
                std::cout << "  Ctrl+Plus/Minus              Volume control\n";
                std::cout << "  Ctrl+Escape                  Quit\n";
#else
                std::cout << "\nGlobal Hotkeys (work anywhere, if X11 available):\n";
                std::cout << "  Ctrl+Alt+N                   Next track\n";
                std::cout << "  Ctrl+Alt+P                   Previous track\n";
                std::cout << "  Ctrl+Alt+R                   Pause/Resume\n";
                std::cout << "  Ctrl+Alt+Plus/Minus          Volume control\n";
                std::cout << "  Ctrl+Alt+Escape              Quit\n";
                std::cout << "\nMedia Keys (work anywhere, including Wayland):\n";
                std::cout << "  Play/Pause, Next, Previous   Via the system media controls\n";
                std::cout << "\nTerminal Hotkeys (when terminal has focus):\n";
                std::cout << "  N/n                          Next track\n";
                std::cout << "  P/p                          Previous track\n";
                std::cout << "  Space/R/r                    Pause/Resume\n";
                std::cout << "  +/-                          Volume control\n";
                std::cout << "  Q/q/ESC                      Quit\n";
#endif
                return 0;
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                std::cerr << "Use --help for usage information\n";
                return 1;
            }
        }
        
        nigamp::MusicPlayer player(preview_mode, gui);
        
        if (!player.initialize()) {
            std::cerr << "Failed to initialize music player\n";
            return 1;
        }
        
        player.run(target_path, is_file);
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}