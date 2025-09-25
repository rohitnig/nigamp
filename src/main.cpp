#include "audio_engine.hpp"
#include "mp3_decoder.hpp"
#include "playlist.hpp"
#include "hotkey_handler.hpp"
#include "file_scanner.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <mutex>

// Simple debug logging
#ifdef DEBUG
    #define DEBUG_LOG(msg) std::cout << "[DEBUG] " << msg << std::endl
#else
    #define DEBUG_LOG(msg) do {} while(0)
#endif

#define INFO_LOG(msg) std::cout << "[INFO] " << msg << std::endl
#define ERROR_LOG(msg) std::cerr << "[ERROR] " << msg << std::endl

namespace nigamp {

class MusicPlayer {
private:
    std::unique_ptr<IAudioEngine> m_audio_engine;
    
    // Helper function to format time as MM:SS
    std::string format_time(double seconds) {
        int minutes = static_cast<int>(seconds) / 60;
        int secs = static_cast<int>(seconds) % 60;
        char buffer[6];
        snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, secs);
        return std::string(buffer);
    }
    std::unique_ptr<IPlaylist> m_playlist;
    std::unique_ptr<IHotkeyHandler> m_hotkey_handler;
    std::unique_ptr<IFileScanner> m_file_scanner;
    std::unique_ptr<IAudioDecoder> m_current_decoder;
    
    std::atomic<bool> m_should_quit{false};
    std::atomic<bool> m_is_paused{false};
    std::atomic<bool> m_advance_to_next{false};
    std::atomic<bool> m_stop_playback{false};
    std::thread m_playback_thread;
    std::thread m_reindex_thread;
    std::thread m_timeout_thread;
    std::mutex m_playlist_mutex;
    
    const Song* m_current_song = nullptr;
    float m_volume = DEFAULT_VOLUME;
    bool m_preview_mode = false;
    
    // Safety timeout mechanism
    std::atomic<bool> m_timeout_active{false};
    std::chrono::steady_clock::time_point m_eof_signaled_time;
    static constexpr int COMPLETION_TIMEOUT_SECONDS = 3;
    
    // Duration-based completion tracking
    std::chrono::steady_clock::time_point m_playback_start_time;
    double m_current_song_duration = 0.0;
    bool m_use_duration_based_completion = true;
    
    // Constants
    static constexpr double DEFAULT_VOLUME = 0.8;
    static constexpr int COUNTDOWN_UPDATE_INTERVAL_MS = 500;
    static constexpr int PREVIEW_DURATION_SECONDS = 10;
    
    // Reindexing properties
    std::string m_current_directory = ".";
    std::chrono::steady_clock::time_point m_last_index_time;
    static constexpr int REINDEX_INTERVAL_MINUTES = 10;

public:
    MusicPlayer(bool preview_mode = false) : m_preview_mode(preview_mode) {
        m_audio_engine = create_audio_engine();
        m_playlist = create_playlist();
        m_hotkey_handler = create_hotkey_handler();
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
            std::cout << "Warning: Failed to register some hotkeys (try running as administrator)\n";
            std::cout << "Player will work without global hotkeys\n";
        } else {
            std::cout << "Global hotkeys registered successfully!\n";
        }
        
        // Start the message loop immediately after hotkey registration
        m_hotkey_handler->process_messages();
        
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
            loaded = load_directory("C:\\Music");
            m_current_directory = "C:\\Music";
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
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
private:
    void start_completion_timeout() {
        m_eof_signaled_time = std::chrono::steady_clock::now();
        m_timeout_active = true;
        
        // Join any existing timeout thread
        if (m_timeout_thread.joinable()) {
            m_timeout_thread.join();
        }
        
        m_timeout_thread = std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::seconds(COMPLETION_TIMEOUT_SECONDS));
            
            if (m_timeout_active.load()) {
                std::cerr << "Warning: Audio completion callback timeout after " 
                          << COMPLETION_TIMEOUT_SECONDS << " seconds. Forcing track advance.\n";
                m_advance_to_next = true;
                m_timeout_active = false;
            }
        });
    }
    
    void handle_playback_completion(const CompletionResult& result) {
        
        // Cancel timeout since callback fired successfully
        m_timeout_active = false;
        
        if (result.error_code != AudioEngineError::SUCCESS) {
            std::cerr << "Audio playback completed with error: " << result.error_message << "\n";
        } else {
            std::cout << "Audio playback completed successfully after " 
                      << result.completion_time.count() << "ms\n";
        }
        
        // Signal track advancement
        m_advance_to_next = true;
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
            std::cout << "Paused\n";
        } else {
            m_audio_engine->resume();
            m_is_paused = false;
            std::cout << "Resumed\n";
        }
    }
    
    void adjust_volume(float delta) {
        m_volume = std::clamp(m_volume + delta, 0.0f, 1.0f);
        m_audio_engine->set_volume(m_volume);
        std::cout << "Volume: " << static_cast<int>(m_volume * 100) << "%\n";
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
        
        // Get song duration for duration-based completion
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
        
        // Note: m_playback_start_time will be set inside playback_loop when it actually starts
        
        m_playback_thread = std::thread(&MusicPlayer::playback_loop, this);
    }
    
    void stop_current_song() {
        
        // Cancel any active timeout thread and reset advancement flags
        m_timeout_active = false;
        m_advance_to_next = false;
        
        // Reset playback state controllers (but preserve pause state)
        m_current_song_duration = 0.0;
        // Note: m_playback_start_time will be reset in play_current_song()
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
        
        // Wait for timeout thread to finish if it's running
        if (m_timeout_thread.joinable()) {
            m_timeout_thread.join();
        }
        
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

    void playback_loop() {
        try {
            AudioBuffer buffer;
            size_t buffer_size = m_audio_engine->get_buffer_size();
            
            auto start_time = std::chrono::steady_clock::now();
            const auto preview_duration = std::chrono::seconds(PREVIEW_DURATION_SECONDS);
            
            // Set playback start time NOW when playback actually begins
            m_playback_start_time = std::chrono::steady_clock::now();
            
            bool preview_completed = false;
            auto last_display_update = std::chrono::steady_clock::now();
            const auto display_update_interval = std::chrono::milliseconds(COUNTDOWN_UPDATE_INTERVAL_MS);
            
            while (!m_stop_playback && !m_should_quit && m_current_decoder) {
                // Check song duration completion (ignore decoder EOF)
                if (m_use_duration_based_completion && m_current_song_duration > 0) {
                    auto elapsed = std::chrono::steady_clock::now() - m_playback_start_time;
                    auto elapsed_seconds = std::chrono::duration<double>(elapsed).count();
                    
                    // Update countdown display periodically
                    auto now = std::chrono::steady_clock::now();
                    if (now - last_display_update >= display_update_interval) {
                        double remaining_seconds = m_current_song_duration - elapsed_seconds;
                        if (remaining_seconds > 0) {
                            std::string status = m_is_paused ? "⏸️  [PAUSED]" : "🎵";
                            std::cout << "\r" << status << " " << (m_current_song ? m_current_song->title : "Unknown") 
                                      << " - Time remaining: " << format_time(remaining_seconds)
                                      << " / " << format_time(m_current_song_duration) << std::flush;
                        }
                        last_display_update = now;
                    }
                    
                    if (elapsed_seconds >= m_current_song_duration) {
                        std::cout << "\r" << std::string(80, ' ') << "\r"; // Clear the line
                        break;
                    }
                }
                
                // Check if preview mode time limit reached
                if (m_preview_mode) {
                    auto elapsed = std::chrono::steady_clock::now() - start_time;
                    
                    // Update preview countdown display
                    auto now = std::chrono::steady_clock::now();
                    if (now - last_display_update >= display_update_interval) {
                        double preview_elapsed = std::chrono::duration<double>(elapsed).count();
                        double preview_remaining = PREVIEW_DURATION_SECONDS - preview_elapsed;
                        if (preview_remaining > 0) {
                            std::cout << "\r🎵 [PREVIEW] " << (m_current_song ? m_current_song->title : "Unknown") 
                                      << " - Time remaining: " << format_time(preview_remaining)
                                      << " / " << format_time(PREVIEW_DURATION_SECONDS) << std::flush;
                        }
                        last_display_update = now;
                    }
                    
                    if (elapsed >= preview_duration) {
                        std::cout << "\r" << std::string(80, ' ') << "\r"; // Clear the line
                        if (m_current_song) {
                            INFO_LOG("Preview complete for: " << m_current_song->title);
                        }
                        preview_completed = true;
                        break;
                    }
                }
                
                if (!m_is_paused) {
                    // Keep decoding even if decoder hits EOF - we'll stop based on duration
                    if (m_current_decoder->decode(buffer, buffer_size)) {
                        m_audio_engine->write_samples(buffer);
                    } else if (m_current_decoder->is_eof()) {
                        // Decoder hit EOF but we continue until duration is reached
                        // Fill buffer with silence to keep audio engine running
                        buffer.assign(buffer_size, 0);
                        m_audio_engine->write_samples(buffer);
                    } else {
                        break;
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            
            // Signal completion - either by duration, preview, or actual completion
            if (m_current_decoder) {
                std::cout << "\r" << std::string(80, ' ') << "\r"; // Clear the countdown line
                m_audio_engine->signal_eof();
                
                // For duration-based completion, immediately advance to next track
                // BUT only if we're not already stopping due to manual track change
                if (m_use_duration_based_completion && !m_stop_playback.load()) {
                    // Give audio engine brief moment to process, then advance
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    m_advance_to_next = true;
                } else if (!m_stop_playback.load()) {
                    // Use traditional timeout mechanism only if not manually stopping
                    start_completion_timeout();
                }
                // If m_stop_playback is true, don't start any completion mechanism
            }
            
            // Note: Track advancement now happens via audio engine callback
            // No need to manually set m_advance_to_next here
            
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
                std::this_thread::sleep_for(std::chrono::minutes(1)); // Check every minute
                
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
        
        // Wait for timeout thread to finish
        m_timeout_active = false;
        if (m_timeout_thread.joinable()) {
            std::cout << "Waiting for timeout thread to finish...\n";
            m_timeout_thread.join();
            std::cout << "Timeout thread finished\n";
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
        std::string target_path = "";
        bool is_file = false;
        
        // Parse command line arguments
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            if (arg == "--preview" || arg == "-p") {
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
                std::cout << "  --help, -h                   Show this help message\n";
                std::cout << "\nUsage Examples:\n";
                std::cout << "  nigamp                       Scan C:\\Music directory for MP3/WAV files\n";
                std::cout << "  nigamp --file song.mp3       Play single file\n";
                std::cout << "  nigamp --folder \"C:\\Music\"   Play all files from folder\n";
                std::cout << "  nigamp -f song.mp3 -p        Play single file in preview mode\n";
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
                return 0;
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                std::cerr << "Use --help for usage information\n";
                return 1;
            }
        }
        
        nigamp::MusicPlayer player(preview_mode);
        
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