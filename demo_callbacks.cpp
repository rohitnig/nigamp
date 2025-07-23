#include "include/audio_engine.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace nigamp;

// Simple demo showing callback behavior
int main() {
    std::cout << "Callback Architecture Demo\n";
    std::cout << "==========================\n\n";
    
    // Create audio engine
    auto engine = create_audio_engine();
    
    // Set up callback
    bool callback_received = false;
    engine->set_completion_callback([&](const CompletionResult& result) {
        std::cout << "🎵 CALLBACK FIRED!\n";
        std::cout << "   Error code: " << static_cast<int>(result.error_code) << "\n";
        std::cout << "   Message: " << result.error_message << "\n";
        std::cout << "   Time: " << result.completion_time.count() << "ms\n";
        std::cout << "   Samples: " << result.samples_processed << "\n\n";
        callback_received = true;
    });
    
    // Initialize with dummy format
    AudioFormat format{44100, 2, 16};
    if (!engine->initialize(format)) {
        std::cerr << "Failed to initialize audio engine\n";
        return 1;
    }
    
    if (!engine->start()) {
        std::cerr << "Failed to start audio engine\n";
        return 1;
    }
    
    std::cout << "1. Writing some audio samples...\n";
    AudioBuffer samples(1000, 100);  // 1000 samples with value 100
    engine->write_samples(samples);
    
    std::cout << "2. Buffered samples: " << engine->get_buffered_samples() << "\n";
    
    std::cout << "3. Signaling EOF (but buffers not empty yet)...\n";
    engine->signal_eof();
    std::cout << "   Callback fired? " << (callback_received ? "YES" : "NO") << "\n\n";
    
    std::cout << "4. Waiting for buffers to drain naturally...\n";
    int wait_count = 0;
    while (engine->get_buffered_samples() > 0 && wait_count < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "   Buffered: " << engine->get_buffered_samples() 
                  << ", Callback: " << (callback_received ? "YES" : "NO") << "\r";
        wait_count++;
    }
    std::cout << "\n\n";
    
    if (callback_received) {
        std::cout << "✅ SUCCESS: Callback fired when buffers emptied!\n";
    } else {
        std::cout << "❌ ISSUE: Callback never fired\n";
        std::cout << "   Final buffered samples: " << engine->get_buffered_samples() << "\n";
    }
    
    engine->stop();
    
    std::cout << "\nDemo complete. This shows:\n";
    std::cout << "- Callback doesn't fire immediately when EOF signaled\n";
    std::cout << "- Callback fires only when EOF + buffers empty\n";
    std::cout << "- This prevents premature track advancement\n";
    
    return 0;
}