#include <gtest/gtest.h>
#include "../include/audio_engine.hpp"
#include <iostream>

using namespace nigamp;

// Very simple test to isolate the callback issue
TEST(SimpleCallback, BasicTest) {
    std::cout << "Starting simple callback test..." << std::endl;
    
    // Test that CompletionResult can be created
    CompletionResult result;
    result.error_code = AudioEngineError::SUCCESS;
    result.error_message = "Test";
    result.completion_time = std::chrono::milliseconds(100);
    result.samples_processed = 1000;
    
    std::cout << "CompletionResult created successfully" << std::endl;
    
    // Test that callback function works
    bool callback_called = false;
    CompletionCallback callback = [&](const CompletionResult& r) {
        std::cout << "Callback executed!" << std::endl;
        callback_called = true;
        EXPECT_EQ(r.error_code, AudioEngineError::SUCCESS);
        EXPECT_EQ(r.samples_processed, 1000);
    };
    
    std::cout << "About to call callback..." << std::endl;
    callback(result);
    
    EXPECT_TRUE(callback_called);
    std::cout << "Simple callback test completed!" << std::endl;
}

// Test that DirectSoundEngine can be created
TEST(SimpleCallback, EngineCreation) {
    std::cout << "Testing engine creation..." << std::endl;
    
    auto engine = create_audio_engine();
    EXPECT_NE(engine, nullptr);
    
    std::cout << "Engine created successfully!" << std::endl;
}