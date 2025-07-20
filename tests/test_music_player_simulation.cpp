#include "../include/hotkey_handler.hpp"
#include "../include/playlist.hpp"
#include "../include/types.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>

using namespace nigamp;

class MockMusicPlayer {
private:
    std::unique_ptr<IPlaylist> m_playlist;
    std::unique_ptr<IHotkeyHandler> m_hotkey_handler;
    
    const Song* m_current_song = nullptr;
    std::atomic<bool> m_is_paused{false};
    std::atomic<bool> m_should_quit{false};
    float m_volume = 0.8f;
    
    // Test tracking
    std::atomic<int> m_next_calls{0};
    std::atomic<int> m_prev_calls{0};
    std::atomic<int> m_pause_calls{0};
    std::atomic<int> m_volume_up_calls{0};
    std::atomic<int> m_volume_down_calls{0};
    std::atomic<int> m_song_changes{0};
    
public:
    MockMusicPlayer() {
        m_playlist = create_playlist();
        m_hotkey_handler = create_hotkey_handler();
        
        // Add some mock songs (simulating files from C:\Music)
        Song song1 = {"Song 1", "Artist 1", "C:\\Music\\mock_song1.mp3", 180};
        Song song2 = {"Song 2", "Artist 2", "C:\\Music\\mock_song2.mp3", 200};
        Song song3 = {"Song 3", "Artist 3", "C:\\Music\\mock_song3.mp3", 220};
        Song song4 = {"Song 4", "Artist 4", "C:\\Music\\mock_song4.mp3", 195};
        Song song5 = {"Song 5", "Artist 5", "C:\\Music\\mock_song5.mp3", 240};
        
        m_playlist->add_song(song1);
        m_playlist->add_song(song2);
        m_playlist->add_song(song3);
        m_playlist->add_song(song4);
        m_playlist->add_song(song5);
        
        std::cout << "[MOCK] Added " << m_playlist->size() << " mock songs to playlist (simulating C:\\Music directory)" << std::endl;
        
        // Set current song to first song
        m_current_song = m_playlist->current();
        if (m_current_song) {
            std::cout << "[MOCK] Starting with: " << m_current_song->title << std::endl;
        }
    }
    
    bool initialize() {
        if (!m_hotkey_handler->initialize()) {
            std::cerr << "[MOCK] Failed to initialize hotkey handler" << std::endl;
            return false;
        }
        
        m_hotkey_handler->set_callback([this](HotkeyAction action) {
            handle_hotkey(action);
        });
        
        bool hotkeys_registered = m_hotkey_handler->register_hotkeys();
        if (hotkeys_registered) {
            std::cout << "[MOCK] Global hotkeys registered successfully!" << std::endl;
        } else {
            std::cout << "[MOCK] Global hotkeys failed to register (this is OK for testing)" << std::endl;
        }
        
        m_hotkey_handler->process_messages();
        return true;
    }
    
    void handle_hotkey(HotkeyAction action) {
        std::cout << "[MOCK] Hotkey action received: " << static_cast<int>(action) << std::endl;
        
        switch (action) {
            case HotkeyAction::NEXT_TRACK:
                m_next_calls++;
                next_track();
                break;
            case HotkeyAction::PREVIOUS_TRACK:
                m_prev_calls++;
                previous_track();
                break;
            case HotkeyAction::PAUSE_RESUME:
                m_pause_calls++;
                toggle_pause();
                break;
            case HotkeyAction::VOLUME_UP:
                m_volume_up_calls++;
                adjust_volume(0.1f);
                break;
            case HotkeyAction::VOLUME_DOWN:
                m_volume_down_calls++;
                adjust_volume(-0.1f);
                break;
            case HotkeyAction::QUIT:
                quit();
                break;
        }
    }
    
    void next_track() {
        const Song* current_before = m_current_song;
        size_t current_index_before = get_current_index();
        
        std::cout << "[MOCK] Next track requested. Current: " 
                  << (m_current_song ? m_current_song->title : "None") 
                  << " (index: " << current_index_before << "/" << (m_playlist->size()-1) << ")" << std::endl;
        
        const Song* next_song = m_playlist->next();
        if (next_song) {
            if (next_song != current_before) {
                m_song_changes++;
                std::cout << "[MOCK] ✓ Song changed to: " << next_song->title 
                          << " (index: " << get_current_index() << ")" << std::endl;
            } else {
                std::cout << "[MOCK] → Song stayed the same (likely at end, wrapped to beginning)" << std::endl;
            }
            m_current_song = next_song;
            play_current_song();
        } else {
            std::cout << "[MOCK] ✗ No next song available" << std::endl;
        }
    }
    
    void previous_track() {
        const Song* current_before = m_current_song;
        size_t current_index_before = get_current_index();
        
        std::cout << "[MOCK] Previous track requested. Current: " 
                  << (m_current_song ? m_current_song->title : "None") 
                  << " (index: " << current_index_before << "/" << (m_playlist->size()-1) << ")" << std::endl;
        
        const Song* prev_song = m_playlist->previous();
        if (prev_song) {
            if (prev_song != current_before) {
                m_song_changes++;
                std::cout << "[MOCK] ✓ Song changed to: " << prev_song->title 
                          << " (index: " << get_current_index() << ")" << std::endl;
            } else {
                std::cout << "[MOCK] → Song stayed the same (likely at beginning, no change)" << std::endl;
            }
            m_current_song = prev_song;
            play_current_song();
        } else {
            std::cout << "[MOCK] ✗ No previous song available" << std::endl;
        }
    }
    
    void toggle_pause() {
        m_is_paused = !m_is_paused;
        std::cout << "[MOCK] " << (m_is_paused ? "Paused" : "Resumed") << std::endl;
    }
    
    void adjust_volume(float delta) {
        float old_volume = m_volume;
        m_volume = std::max(0.0f, std::min(1.0f, m_volume + delta));
        std::cout << "[MOCK] Volume: " << static_cast<int>(old_volume * 100) 
                  << "% → " << static_cast<int>(m_volume * 100) << "%" << std::endl;
    }
    
    void quit() {
        std::cout << "[MOCK] Quit requested" << std::endl;
        m_should_quit = true;
    }
    
    void play_current_song() {
        if (m_current_song) {
            std::cout << "[MOCK] ♪ Now playing: " << m_current_song->title 
                      << " by " << m_current_song->artist << std::endl;
        }
    }
    
    size_t get_current_index() {
        // Simple way to find current index (playlist interface doesn't expose this directly)
        if (!m_current_song) return 0;
        
        size_t index = 0;
        // This is a bit hacky but works for testing
        const Song* temp_current = m_playlist->current();
        
        // Reset to beginning and count
        while (m_playlist->previous()) { /* go to start */ }
        
        while (true) {
            const Song* song = m_playlist->current();
            if (song && song->file_path == m_current_song->file_path) {
                // Restore original position
                while (m_playlist->current() != temp_current && m_playlist->next()) { /* restore */ }
                return index;
            }
            if (!m_playlist->next()) break;
            index++;
        }
        
        // Restore original position
        while (m_playlist->current() != temp_current && m_playlist->previous()) { /* restore */ }
        return 0;
    }
    
    void run_manual_test() {
        std::cout << "\n[TEST] Starting manual hotkey test..." << std::endl;
        std::cout << "[TEST] Try these test scenarios:" << std::endl;
        std::cout << "[TEST] 1. Press 'previous' at first song (should stay at first)" << std::endl;
        std::cout << "[TEST] 2. Press 'next' several times to reach last song" << std::endl;
        std::cout << "[TEST] 3. Press 'next' at last song (should wrap to first)" << std::endl;
        std::cout << "[TEST] 4. Test pause/resume and volume controls" << std::endl;
        std::cout << "[TEST] Global: Ctrl+Alt+N/P/R, Local: Ctrl+N/P/R (focus this window)" << std::endl;
        
        reset_test_counters();
        
        for (int i = 20; i > 0 && !m_should_quit; i--) {
            std::cout << "[TEST] " << i << " seconds remaining..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        print_test_results();
    }
    
    void run_automated_test() {
        std::cout << "\n[TEST] Starting automated playlist navigation test..." << std::endl;
        reset_test_counters();
        
        // Test 1: Previous at first song
        std::cout << "\n--- Test 1: Previous at first song ---" << std::endl;
        while (m_playlist->previous()) { /* go to start */ }
        m_current_song = m_playlist->current();
        std::cout << "[TEST] At first song: " << (m_current_song ? m_current_song->title : "None") << std::endl;
        
        const Song* first_song = m_current_song;
        previous_track();
        
        if (m_current_song == first_song) {
            std::cout << "[TEST] ✓ PASS: Previous at first song correctly stayed at first" << std::endl;
        } else {
            std::cout << "[TEST] ✗ FAIL: Previous at first song incorrectly changed song" << std::endl;
        }
        
        // Test 2: Navigate to last song
        std::cout << "\n--- Test 2: Navigate to last song ---" << std::endl;
        size_t playlist_size = m_playlist->size();
        std::cout << "[TEST] Playlist size: " << playlist_size << std::endl;
        
        // Go to last song
        while (m_playlist->next()) { /* go to end */ }
        m_current_song = m_playlist->current();
        std::cout << "[TEST] At last song: " << (m_current_song ? m_current_song->title : "None") << std::endl;
        
        // Test 3: Next at last song (should wrap to first)
        std::cout << "\n--- Test 3: Next at last song ---" << std::endl;
        const Song* last_song = m_current_song;
        next_track();
        
        // Check if we wrapped to first song
        if (m_current_song != last_song) {
            std::cout << "[TEST] ✓ PASS: Next at last song correctly wrapped to: " 
                      << (m_current_song ? m_current_song->title : "None") << std::endl;
        } else {
            std::cout << "[TEST] ? INFO: Next at last song stayed at last (implementation dependent)" << std::endl;
        }
        
        print_test_results();
    }
    
    void reset_test_counters() {
        m_next_calls = 0;
        m_prev_calls = 0;
        m_pause_calls = 0;
        m_volume_up_calls = 0;
        m_volume_down_calls = 0;
        m_song_changes = 0;
    }
    
    void print_test_results() {
        std::cout << "\n[TEST RESULTS]" << std::endl;
        std::cout << "Next calls:     " << m_next_calls << std::endl;
        std::cout << "Previous calls: " << m_prev_calls << std::endl;
        std::cout << "Pause calls:    " << m_pause_calls << std::endl;
        std::cout << "Volume up:      " << m_volume_up_calls << std::endl;
        std::cout << "Volume down:    " << m_volume_down_calls << std::endl;
        std::cout << "Song changes:   " << m_song_changes << std::endl;
        std::cout << "Current song:   " << (m_current_song ? m_current_song->title : "None") << std::endl;
        std::cout << "Is paused:      " << (m_is_paused ? "Yes" : "No") << std::endl;
        std::cout << "Volume:         " << static_cast<int>(m_volume * 100) << "%" << std::endl;
    }
    
    void shutdown() {
        m_hotkey_handler->shutdown();
    }
    
    bool should_quit() const { return m_should_quit; }
};

int main() {
    std::cout << "=== Music Player Simulation Test ===" << std::endl;
    
    MockMusicPlayer player;
    
    if (!player.initialize()) {
        std::cerr << "[TEST ERROR] Failed to initialize mock player" << std::endl;
        return 1;
    }
    
    std::cout << "\nChoose test mode:" << std::endl;
    std::cout << "1. Automated test (tests edge cases automatically)" << std::endl;
    std::cout << "2. Manual test (20 seconds to test hotkeys)" << std::endl;
    std::cout << "3. Both tests" << std::endl;
    std::cout << "Enter choice (1-3): ";
    
    int choice;
    std::cin >> choice;
    
    switch (choice) {
        case 1:
            player.run_automated_test();
            break;
        case 2:
            player.run_manual_test();
            break;
        case 3:
        default:
            player.run_automated_test();
            if (!player.should_quit()) {
                player.run_manual_test();
            }
            break;
    }
    
    player.shutdown();
    
    std::cout << "\n[TEST] Music player simulation test completed!" << std::endl;
    return 0;
}