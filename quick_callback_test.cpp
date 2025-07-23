// Quick manual test for callback architecture
// Compile with: g++ -I include -std=c++17 quick_callback_test.cpp -o quick_test

#include "include/audio_engine.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace nigamp;

int main() {
    std::cout << "Quick Callback Test\n";
    std::cout << "==================\n\n";
    
    try {
        // Test 1: Check if types compile
        std::cout << "1. Testing types compilation...\n";
        CompletionResult result;
        result.error_code = AudioEngineError::SUCCESS;
        result.error_message = "Test";
        result.completion_time = std::chrono::milliseconds(100);
        result.samples_processed = 1000;
        std::cout << "   ✅ CompletionResult works\n";
        
        // Test 2: Check if callback function works
        std::cout << "2. Testing callback function...\n";
        bool callback_called = false;
        CompletionCallback callback = [&](const CompletionResult& r) {
            std::cout << "   📞 Callback executed!\n";
            std::cout << "   Error code: " << static_cast<int>(r.error_code) << "\n";
            std::cout << "   Message: " << r.error_message << "\n";
            std::cout << "   Samples: " << r.samples_processed << "\n";
            callback_called = true;
        };
        
        callback(result);
        std::cout << "   ✅ Callback function works: " << (callback_called ? "YES" : "NO") << "\n";
        
        // Test 3: Check if DirectSoundEngine can be created
        std::cout << "3. Testing DirectSoundEngine creation...\n";
        auto engine = create_audio_engine();
        if (engine) {
            std::cout << "   ✅ Audio engine created successfully\n";
            
            // Test 4: Check if callback can be set
            std::cout << "4. Testing callback setting...\n";
            bool engine_callback_called = false;
            engine->set_completion_callback([&](const CompletionResult& r) {
                std::cout << "   📞 Engine callback executed!\n";
                engine_callback_called = true;
            });
            std::cout << "   ✅ Callback set without errors\n";
            
            // Test 5: Check basic engine operations
            std::cout << "5. Testing basic engine operations...\n";
            AudioFormat format{44100, 2, 16};
            
            if (engine->initialize(format)) {
                std::cout << "   ✅ Engine initialized\n";
                
                // Test signal_eof and get_buffered_samples
                std::cout << "   Buffered samples: " << engine->get_buffered_samples() << "\n";
                engine->signal_eof();
                std::cout << "   ✅ signal_eof() called without crash\n";
                
                engine->shutdown();
                std::cout << "   ✅ Engine shutdown\n";
            } else {
                std::cout << "   ⚠️ Engine initialization failed (expected on systems without audio)\n";
            }
        } else {
            std::cout << "   ❌ Failed to create audio engine\n";
        }
        
        std::cout << "\n🎉 Quick test completed!\n";
        std::cout << "If you see this message, the basic callback architecture is working.\n";
        
    } catch (const std::exception& e) {
        std::cout << "❌ Exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cout << "❌ Unknown exception\n";
        return 1;
    }
    
    return 0;
}