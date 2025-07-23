#include <gtest/gtest.h>
#include "../include/audio_engine.hpp"
#include <iostream>
#include <atomic>

using namespace nigamp;

// Very simple mock that avoids deadlocks
class SimpleMockEngine : public IAudioEngine {
private:
    CompletionCallback m_callback;
    std::atomic<bool> m_eof_signaled{false};
    std::atomic<bool> m_callback_fired{false};
    std::atomic<size_t> m_buffer_samples{0};
    size_t m_total_samples = 0;
    
public:
    bool initialize(const AudioFormat& format) override { return true; }
    bool start() override { 
        m_eof_signaled = false;
        m_callback_fired = false;
        m_buffer_samples = 0;
        return true; 
    }
    bool stop() override { return true; }
    bool pause() override { return true; }
    bool resume() override { return true; }
    void shutdown() override {}
    
    bool write_samples(const AudioBuffer& buffer) override {
        m_buffer_samples += buffer.size();
        m_total_samples += buffer.size();
        return true;
    }
    
    size_t get_buffer_size() const override { return 1024; }
    bool is_playing() const override { return true; }
    void set_volume(float volume) override {}
    float get_volume() const override { return 1.0f; }
    
    void set_completion_callback(CompletionCallback callback) override {
        m_callback = callback;
    }
    
    void signal_eof() override {
        std::cout << "SimpleMock: EOF signaled" << std::endl;
        m_eof_signaled = true;
        check_and_fire_callback();
    }
    
    size_t get_buffered_samples() const override {
        return m_buffer_samples.load();
    }
    
    // Test helper - simulates buffer draining
    void drain_buffers() {
        std::cout << "SimpleMock: Draining buffers" << std::endl;
        m_buffer_samples = 0;
        check_and_fire_callback();
    }
    
private:
    void check_and_fire_callback() {
        if (m_eof_signaled.load() && m_buffer_samples.load() == 0 && 
            !m_callback_fired.exchange(true) && m_callback) {
            
            std::cout << "SimpleMock: Firing callback" << std::endl;
            
            CompletionResult result;
            result.error_code = AudioEngineError::SUCCESS;
            result.error_message = "Mock completion";
            result.completion_time = std::chrono::milliseconds(50);
            result.samples_processed = m_total_samples;
            
            m_callback(result);
            std::cout << "SimpleMock: Callback completed" << std::endl;
        }
    }
};

TEST(CallbackSimple, BasicFlow) {
    std::cout << "=== Starting BasicFlow test ===" << std::endl;
    
    SimpleMockEngine engine;
    bool callback_received = false;
    CompletionResult result_received;
    
    // Set callback
    std::cout << "Setting callback..." << std::endl;
    engine.set_completion_callback([&](const CompletionResult& result) {
        std::cout << "*** CALLBACK RECEIVED ***" << std::endl;
        callback_received = true;
        result_received = result;
    });
    
    // Start engine
    std::cout << "Starting engine..." << std::endl;
    EXPECT_TRUE(engine.start());
    
    // Write samples
    std::cout << "Writing samples..." << std::endl;
    AudioBuffer test_buffer(1000, 42);  // 1000 samples, value 42
    EXPECT_TRUE(engine.write_samples(test_buffer));
    EXPECT_EQ(engine.get_buffered_samples(), 1000);
    
    // Signal EOF (callback should NOT fire yet)
    std::cout << "Signaling EOF..." << std::endl;
    engine.signal_eof();
    EXPECT_FALSE(callback_received) << "Callback fired too early!";
    
    // Drain buffers (callback SHOULD fire now)
    std::cout << "Draining buffers..." << std::endl;
    engine.drain_buffers();
    
    // Check results
    std::cout << "Checking results..." << std::endl;
    EXPECT_TRUE(callback_received) << "Callback was never fired!";
    
    if (callback_received) {
        EXPECT_EQ(result_received.error_code, AudioEngineError::SUCCESS);
        EXPECT_EQ(result_received.samples_processed, 1000);
        std::cout << "SUCCESS: All checks passed!" << std::endl;
    }
    
    std::cout << "=== BasicFlow test completed ===" << std::endl;
}

TEST(CallbackSimple, NoCallbackWithoutEOF) {
    std::cout << "=== Starting NoCallbackWithoutEOF test ===" << std::endl;
    
    SimpleMockEngine engine;
    bool callback_received = false;
    
    engine.set_completion_callback([&](const CompletionResult& result) {
        callback_received = true;
    });
    
    engine.start();
    AudioBuffer test_buffer(500, 1);
    engine.write_samples(test_buffer);
    
    // Drain buffers without signaling EOF
    engine.drain_buffers();
    
    EXPECT_FALSE(callback_received) << "Callback should not fire without EOF!";
    
    std::cout << "=== NoCallbackWithoutEOF test completed ===" << std::endl;
}

TEST(CallbackSimple, NoCallbackWithPendingSamples) {
    std::cout << "=== Starting NoCallbackWithPendingSamples test ===" << std::endl;
    
    SimpleMockEngine engine;
    bool callback_received = false;
    
    engine.set_completion_callback([&](const CompletionResult& result) {
        callback_received = true;
    });
    
    engine.start();
    AudioBuffer test_buffer(500, 1);
    engine.write_samples(test_buffer);
    
    // Signal EOF but don't drain buffers
    engine.signal_eof();
    
    EXPECT_FALSE(callback_received) << "Callback should not fire with pending samples!";
    
    std::cout << "=== NoCallbackWithPendingSamples test completed ===" << std::endl;
}