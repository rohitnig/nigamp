#include <gtest/gtest.h>
#include "../include/audio_engine.hpp"
#include <atomic>
#include <thread>
#include <chrono>

using namespace nigamp;

// Mock audio engine for testing callback architecture
class MockAudioEngine : public IAudioEngine {
private:
    CompletionCallback m_completion_callback;
    std::atomic<bool> m_eof_signaled{false};
    std::atomic<bool> m_callback_fired{false};
    AudioBuffer m_pending_samples;
    mutable std::mutex m_buffer_mutex;
    mutable std::mutex m_callback_mutex;
    size_t m_total_samples_processed = 0;
    std::chrono::steady_clock::time_point m_start_time;

public:
    MockAudioEngine() : m_start_time(std::chrono::steady_clock::now()) {}
    
    bool initialize(const AudioFormat& format) override { return true; }
    bool start() override { 
        m_eof_signaled = false;
        m_callback_fired = false;
        m_total_samples_processed = 0;
        m_start_time = std::chrono::steady_clock::now();
        return true; 
    }
    bool stop() override { 
        std::lock_guard<std::mutex> lock(m_callback_mutex);
        m_completion_callback = nullptr;
        return true; 
    }
    bool pause() override { return true; }
    bool resume() override { return true; }
    void shutdown() override {}
    
    bool write_samples(const AudioBuffer& buffer) override {
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
        m_pending_samples.insert(m_pending_samples.end(), buffer.begin(), buffer.end());
        m_total_samples_processed += buffer.size();
        return true;
    }
    
    size_t get_buffer_size() const override { return 1024; }
    bool is_playing() const override { return true; }
    void set_volume(float volume) override {}
    float get_volume() const override { return 1.0f; }
    
    void set_completion_callback(CompletionCallback callback) override {
        std::lock_guard<std::mutex> lock(m_callback_mutex);
        m_completion_callback = callback;
    }
    
    void signal_eof() override {
        m_eof_signaled = true;
        // Immediately check completion since this is a mock
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
        check_completion();
    }
    
    size_t get_buffered_samples() const override {
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
        return m_pending_samples.size();
    }
    
    // Test helpers
    void drain_all_buffers() {
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
        m_pending_samples.clear();
        check_completion();
    }
    
    bool callback_fired() const { return m_callback_fired.load(); }
    
private:
    void check_completion() {
        std::lock_guard<std::mutex> buffer_lock(m_buffer_mutex);
        if (m_eof_signaled.load() && m_pending_samples.empty()) {
            fire_completion_callback();
        }
    }
    
    void fire_completion_callback() {
        std::lock_guard<std::mutex> lock(m_callback_mutex);
        if (!m_callback_fired.exchange(true) && m_completion_callback) {
            auto completion_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_start_time);
                
            CompletionResult result;
            result.error_code = AudioEngineError::SUCCESS;
            result.error_message = "Test completion";
            result.completion_time = completion_time;
            result.samples_processed = m_total_samples_processed;
            
            m_completion_callback(result);
        }
    }
};

// Helper function to create test audio data
AudioBuffer create_test_audio_buffer(size_t samples) {
    AudioBuffer buffer(samples);
    for (size_t i = 0; i < samples; ++i) {
        buffer[i] = static_cast<int16_t>(i % 1000);
    }
    return buffer;
}

// Test basic callback functionality
TEST(CallbackArchitecture, BasicCompletionFlow) {
    std::cout << "Starting BasicCompletionFlow test..." << std::endl;
    
    MockAudioEngine engine;
    bool callback_fired = false;
    CompletionResult received_result;
    
    std::cout << "Setting callback..." << std::endl;
    engine.set_completion_callback([&](const CompletionResult& result) {
        std::cout << "Callback received!" << std::endl;
        callback_fired = true;
        received_result = result;
    });
    
    std::cout << "Starting engine..." << std::endl;
    engine.start();
    
    std::cout << "Writing samples..." << std::endl;
    engine.write_samples(create_test_audio_buffer(1000));
    
    std::cout << "Signaling EOF..." << std::endl;
    engine.signal_eof();
    
    std::cout << "Draining buffers..." << std::endl;
    engine.drain_all_buffers();
    
    std::cout << "Checking results..." << std::endl;
    EXPECT_TRUE(callback_fired) << "Callback was not fired";
    if (callback_fired) {
        EXPECT_EQ(received_result.error_code, AudioEngineError::SUCCESS);
        EXPECT_EQ(received_result.samples_processed, 1000);
    }
    
    std::cout << "BasicCompletionFlow test completed!" << std::endl;
}

// Test that callback doesn't fire without EOF
TEST(CallbackArchitecture, NoCallbackWithoutEOF) {
    MockAudioEngine engine;
    bool callback_fired = false;
    
    engine.set_completion_callback([&](const CompletionResult& result) {
        callback_fired = true;
    });
    
    engine.start();
    engine.write_samples(create_test_audio_buffer(1000));
    // Don't signal EOF
    engine.drain_all_buffers();
    
    EXPECT_FALSE(callback_fired);
}

// Test that callback doesn't fire with EOF but pending samples
TEST(CallbackArchitecture, NoCallbackWithPendingSamples) {
    MockAudioEngine engine;
    bool callback_fired = false;
    
    engine.set_completion_callback([&](const CompletionResult& result) {
        callback_fired = true;
    });
    
    engine.start();
    engine.write_samples(create_test_audio_buffer(1000));
    engine.signal_eof();
    // Don't drain buffers
    
    EXPECT_FALSE(callback_fired);
}

// Test multiple EOF signals only fire callback once
TEST(CallbackArchitecture, MultipleEOFSignals) {
    MockAudioEngine engine;
    int callback_count = 0;
    
    engine.set_completion_callback([&](const CompletionResult& result) {
        callback_count++;
    });
    
    engine.start();
    engine.signal_eof();
    engine.signal_eof();  // Should be ignored
    engine.signal_eof();  // Should be ignored
    engine.drain_all_buffers();
    
    EXPECT_EQ(callback_count, 1);
}

// Test callback exception safety
TEST(CallbackArchitecture, CallbackExceptionSafety) {
    MockAudioEngine engine;
    
    engine.set_completion_callback([&](const CompletionResult& result) {
        throw std::runtime_error("Test exception");
    });
    
    engine.start();
    engine.signal_eof();
    
    // Should not throw
    EXPECT_NO_THROW(engine.drain_all_buffers());
}

// Test threading safety (basic)
TEST(CallbackArchitecture, BasicThreadingSafety) {
    MockAudioEngine engine;
    std::atomic<int> callback_count{0};
    
    engine.set_completion_callback([&](const CompletionResult& result) {
        callback_count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
    
    engine.start();
    
    // Multiple threads trying to trigger completion
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&]() {
            engine.signal_eof();
            engine.drain_all_buffers();
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(callback_count.load(), 1);  // Only one callback despite race
}