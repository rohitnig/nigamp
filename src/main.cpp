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

namespace nigamp {

class MusicPlayer {
private:
    std::unique_ptr<IAudioEngine> m_audio_engine;
    std::unique_ptr<IPlaylist> m_playlist;
    std::unique_ptr<IHotkeyHandler> m_hotkey_handler;
    std::unique_ptr<IFileScanner> m_file_scanner;
    std::unique_ptr<IAudioDecoder> m_current_decoder;
    
    std::atomic<bool> m_should_quit{false};
    std::atomic<bool> m_is_paused{false};
    std::atomic<bool> m_advance_to_next{false};
    std::thread m_playback_thread;
    std::thread m_reindex_thread;
    std::mutex m_playlist_mutex;
    
    const Song* m_current_song = nullptr;
    float m_volume = 0.8f;
    bool m_preview_mode = false;
    
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
        std::cout << "Hotkeys:\n";
        std::cout << "  Ctrl+Alt+N      - Next track\n";
        std::cout << "  Ctrl+Alt+P      - Previous track\n";
        std::cout << "  Ctrl+Alt+R      - Pause/Resume\n";
        std::cout << "  Ctrl+Alt+Plus   - Volume up\n";
        std::cout << "  Ctrl+Alt+Minus  - Volume down\n";
        std::cout << "  Ctrl+Alt+Escape - Quit\n";
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
            loaded = load_directory(".");
            m_current_directory = ".";
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
    void handle_hotkey(HotkeyAction action) {
        std::cout << "[HOTKEY DEBUG] Main application handle_hotkey called with action: " << static_cast<int>(action) << std::endl;
        
        switch (action) {
            case HotkeyAction::NEXT_TRACK:
                std::cout << "[HOTKEY DEBUG] Executing next_track()" << std::endl;
                next_track();
                break;
            case HotkeyAction::PREVIOUS_TRACK:
                std::cout << "[HOTKEY DEBUG] Executing previous_track()" << std::endl;
                previous_track();
                break;
            case HotkeyAction::PAUSE_RESUME:
                std::cout << "[HOTKEY DEBUG] Executing toggle_pause()" << std::endl;
                toggle_pause();
                break;
            case HotkeyAction::VOLUME_UP:
                std::cout << "[HOTKEY DEBUG] Executing adjust_volume(+0.1)" << std::endl;
                adjust_volume(0.1f);
                break;
            case HotkeyAction::VOLUME_DOWN:
                std::cout << "[HOTKEY DEBUG] Executing adjust_volume(-0.1)" << std::endl;
                adjust_volume(-0.1f);
                break;
            case HotkeyAction::QUIT:
                std::cout << "[HOTKEY DEBUG] Executing quit()" << std::endl;
                quit();
                break;
        }
        
        std::cout << "[HOTKEY DEBUG] Main application handle_hotkey completed" << std::endl;
    }
    
    void handle_track_advance() {
        std::lock_guard<std::mutex> lock(m_playlist_mutex);
        
        const Song* next_song = m_playlist->next();
        if (next_song) {
            std::cout << "Auto-advancing to next track: " << next_song->title << "\n";
            stop_current_song();
            m_current_song = next_song;
            play_current_song();
        } else {
            std::cout << "No more tracks, staying on current song\n";
        }
    }
    
    void next_track() {
        std::lock_guard<std::mutex> lock(m_playlist_mutex);
        const Song* next_song = m_playlist->next();
        if (next_song) {
            stop_current_song();
            m_current_song = next_song;
            play_current_song();
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
            std::cerr << "Failed to open: " << m_current_song->file_path << "\n";
            return;
        }
        
        AudioFormat format = m_current_decoder->get_format();
        if (!m_audio_engine->initialize(format)) {
            std::cerr << "Failed to initialize audio engine\n";
            return;
        }
        
        m_audio_engine->set_volume(m_volume);
        
        if (!m_audio_engine->start()) {
            std::cerr << "Failed to start audio engine\n";
            return;
        }
        
        m_playback_thread = std::thread(&MusicPlayer::playback_loop, this);
    }
    
    void stop_current_song() {
        // Signal playback thread to stop if it's running
        if (m_playback_thread.joinable()) {
            // Audio engine stop will cause playback loop to exit
            if (m_audio_engine) {
                m_audio_engine->stop();
            }
            
            // Wait for thread to finish
            m_playback_thread.join();
        }
        
        if (m_current_decoder) {
            m_current_decoder->close();
            m_current_decoder.reset();
        }
    }
    
    void playback_loop() {
        try {
            AudioBuffer buffer;
            size_t buffer_size = m_audio_engine->get_buffer_size();
            
            auto start_time = std::chrono::steady_clock::now();
            const auto preview_duration = std::chrono::seconds(10);
            
            bool song_completed = false;
            bool preview_completed = false;
            
            while (!m_should_quit && m_current_decoder && !m_current_decoder->is_eof()) {
                // Check if preview mode time limit reached
                if (m_preview_mode) {
                    auto elapsed = std::chrono::steady_clock::now() - start_time;
                    if (elapsed >= preview_duration) {
                        if (m_current_song) {
                            std::cout << "Preview complete for: " << m_current_song->title << "\n";
                        }
                        preview_completed = true;
                        break;
                    }
                }
                
                if (!m_is_paused) {
                    if (m_current_decoder->decode(buffer, buffer_size)) {
                        m_audio_engine->write_samples(buffer);
                    } else {
                        std::cout << "Decode failed, ending playback\n";
                        break;
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            // Check why the loop ended
            if (m_current_decoder && m_current_decoder->is_eof()) {
                std::cout << "Song completed normally (EOF reached)\n";
                song_completed = true;
            } else if (m_should_quit) {
                std::cout << "Playback stopped due to quit signal\n";
            } else if (!m_current_decoder) {
                std::cout << "Playback stopped due to decoder loss\n";
            }
            
            // Only advance to next track if song actually completed or preview finished
            if (!m_should_quit && (song_completed || preview_completed)) {
                std::cout << "Signaling track advance...\n";
                m_advance_to_next = true;
            }
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
                std::cout << "  nigamp                       Scan current directory for MP3/WAV files\n";
                std::cout << "  nigamp --file song.mp3       Play single file\n";
                std::cout << "  nigamp --folder \"C:\\Music\"   Play all files from folder\n";
                std::cout << "  nigamp -f song.mp3 -p        Play single file in preview mode\n";
                std::cout << "\nHotkeys:\n";
                std::cout << "  Ctrl+Alt+N                   Next track\n";
                std::cout << "  Ctrl+Alt+P                   Previous track\n";
                std::cout << "  Ctrl+Alt+R                   Pause/Resume\n";
                std::cout << "  Ctrl+Alt+Plus/Minus          Volume control\n";
                std::cout << "  Ctrl+Alt+Escape              Quit\n";
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