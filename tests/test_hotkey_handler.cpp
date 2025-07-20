#include "../include/hotkey_handler.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

using namespace nigamp;

class HotkeyTester {
private:
    std::atomic<int> next_track_count{0};
    std::atomic<int> previous_track_count{0};
    std::atomic<int> pause_resume_count{0};
    std::atomic<int> volume_up_count{0};
    std::atomic<int> volume_down_count{0};
    std::atomic<int> quit_count{0};
    
public:
    void handle_hotkey_action(HotkeyAction action) {
        std::cout << "[TEST] Received hotkey action: " << static_cast<int>(action) << std::endl;
        
        switch (action) {
            case HotkeyAction::NEXT_TRACK:
                next_track_count++;
                std::cout << "[TEST] NEXT_TRACK triggered (count: " << next_track_count << ")" << std::endl;
                break;
            case HotkeyAction::PREVIOUS_TRACK:
                previous_track_count++;
                std::cout << "[TEST] PREVIOUS_TRACK triggered (count: " << previous_track_count << ")" << std::endl;
                break;
            case HotkeyAction::PAUSE_RESUME:
                pause_resume_count++;
                std::cout << "[TEST] PAUSE_RESUME triggered (count: " << pause_resume_count << ")" << std::endl;
                break;
            case HotkeyAction::VOLUME_UP:
                volume_up_count++;
                std::cout << "[TEST] VOLUME_UP triggered (count: " << volume_up_count << ")" << std::endl;
                break;
            case HotkeyAction::VOLUME_DOWN:
                volume_down_count++;
                std::cout << "[TEST] VOLUME_DOWN triggered (count: " << volume_down_count << ")" << std::endl;
                break;
            case HotkeyAction::QUIT:
                quit_count++;
                std::cout << "[TEST] QUIT triggered (count: " << quit_count << ")" << std::endl;
                break;
        }
    }
    
    void print_results() {
        std::cout << "\n[TEST RESULTS]" << std::endl;
        std::cout << "Next Track:     " << next_track_count << std::endl;
        std::cout << "Previous Track: " << previous_track_count << std::endl;
        std::cout << "Pause/Resume:   " << pause_resume_count << std::endl;
        std::cout << "Volume Up:      " << volume_up_count << std::endl;
        std::cout << "Volume Down:    " << volume_down_count << std::endl;
        std::cout << "Quit:           " << quit_count << std::endl;
    }
    
    void reset_counts() {
        next_track_count = 0;
        previous_track_count = 0;
        pause_resume_count = 0;
        volume_up_count = 0;
        volume_down_count = 0;
        quit_count = 0;
    }
    
    // Direct test methods to verify callback functionality
    void test_all_actions() {
        std::cout << "\n[TEST] Testing all hotkey actions directly..." << std::endl;
        reset_counts();
        
        handle_hotkey_action(HotkeyAction::NEXT_TRACK);
        handle_hotkey_action(HotkeyAction::PREVIOUS_TRACK);
        handle_hotkey_action(HotkeyAction::PAUSE_RESUME);
        handle_hotkey_action(HotkeyAction::VOLUME_UP);
        handle_hotkey_action(HotkeyAction::VOLUME_DOWN);
        handle_hotkey_action(HotkeyAction::QUIT);
        
        print_results();
        
        // Verify all actions were called exactly once
        bool all_passed = (next_track_count == 1 && 
                          previous_track_count == 1 && 
                          pause_resume_count == 1 && 
                          volume_up_count == 1 && 
                          volume_down_count == 1 && 
                          quit_count == 1);
        
        std::cout << "[TEST] Direct action test: " << (all_passed ? "PASSED" : "FAILED") << std::endl;
    }
};

int main() {
    std::cout << "=== Hotkey Handler Test ===" << std::endl;
    
    HotkeyTester tester;
    
    // Test 1: Direct callback functionality
    tester.test_all_actions();
    
    // Test 2: Integration test with actual hotkey handler
    std::cout << "\n[TEST] Testing hotkey handler integration..." << std::endl;
    
    auto hotkey_handler = create_hotkey_handler();
    
    if (!hotkey_handler->initialize()) {
        std::cerr << "[TEST ERROR] Failed to initialize hotkey handler" << std::endl;
        return 1;
    }
    
    // Set up the callback
    hotkey_handler->set_callback([&tester](HotkeyAction action) {
        tester.handle_hotkey_action(action);
    });
    
    // Try to register hotkeys (may fail if not admin, but that's OK for testing)
    bool hotkeys_registered = hotkey_handler->register_hotkeys();
    if (hotkeys_registered) {
        std::cout << "[TEST] Global hotkeys registered successfully!" << std::endl;
    } else {
        std::cout << "[TEST] Global hotkeys failed to register (this is OK for testing)" << std::endl;
    }
    
    // Start the message processing
    hotkey_handler->process_messages();
    
    std::cout << "\n[TEST] Hotkey handler is now running..." << std::endl;
    std::cout << "[TEST] Try pressing hotkeys for 10 seconds:" << std::endl;
    std::cout << "[TEST] Global: Ctrl+Alt+N, Ctrl+Alt+P, Ctrl+Alt+R, etc." << std::endl;
    std::cout << "[TEST] Local: Ctrl+N, Ctrl+P, Ctrl+R, etc. (when this console is focused)" << std::endl;
    
    tester.reset_counts();
    
    // Wait for 10 seconds to let user test hotkeys
    for (int i = 10; i > 0; i--) {
        std::cout << "[TEST] " << i << " seconds remaining..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    std::cout << "\n[TEST] Test period ended. Results:" << std::endl;
    tester.print_results();
    
    // Clean up
    hotkey_handler->shutdown();
    
    std::cout << "\n[TEST] Hotkey handler test completed!" << std::endl;
    return 0;
}