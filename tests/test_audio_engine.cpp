#include <gtest/gtest.h>
#include "../src/audio_engine.cpp"
#include <thread>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class AudioEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = nigamp::create_audio_engine();
        
        format.sample_rate = 44100;
        format.channels = 2;
        format.bits_per_sample = 16;
    }
    
    std::unique_ptr<nigamp::IAudioEngine> engine;
    nigamp::AudioFormat format;
};

TEST_F(AudioEngineTest, Initialization) {
    ASSERT_NE(engine, nullptr);
    
    bool initialized = engine->initialize(format);
    EXPECT_TRUE(initialized);
    
    EXPECT_GT(engine->get_buffer_size(), 0);
    
    engine->shutdown();
}

TEST_F(AudioEngineTest, VolumeControl) {
    engine->initialize(format);
    
    engine->set_volume(0.5f);
    EXPECT_FLOAT_EQ(engine->get_volume(), 0.5f);
    
    engine->set_volume(1.2f);
    EXPECT_FLOAT_EQ(engine->get_volume(), 1.0f);
    
    engine->set_volume(-0.1f);
    EXPECT_FLOAT_EQ(engine->get_volume(), 0.0f);
    
    engine->shutdown();
}

TEST_F(AudioEngineTest, PlaybackControl) {
    engine->initialize(format);
    
    EXPECT_FALSE(engine->is_playing());
    
    bool started = engine->start();
    EXPECT_TRUE(started);
    EXPECT_TRUE(engine->is_playing());
    
    bool paused = engine->pause();
    EXPECT_TRUE(paused);
    EXPECT_FALSE(engine->is_playing());
    
    bool resumed = engine->resume();
    EXPECT_TRUE(resumed);
    EXPECT_TRUE(engine->is_playing());
    
    bool stopped = engine->stop();
    EXPECT_TRUE(stopped);
    EXPECT_FALSE(engine->is_playing());
    
    engine->shutdown();
}

TEST_F(AudioEngineTest, WriteAudioData) {
    engine->initialize(format);
    engine->start();
    
    nigamp::AudioBuffer buffer(1024);
    for (size_t i = 0; i < buffer.size(); i += 2) {
        buffer[i] = static_cast<int16_t>(sin(2.0 * M_PI * 440.0 * i / format.sample_rate) * 16000);
        buffer[i + 1] = buffer[i];
    }
    
    bool written = engine->write_samples(buffer);
    EXPECT_TRUE(written);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    engine->stop();
    engine->shutdown();
}

TEST_F(AudioEngineTest, MultipleFormats) {
    nigamp::AudioFormat format1{22050, 1, 16};
    nigamp::AudioFormat format2{48000, 2, 16};
    
    EXPECT_TRUE(engine->initialize(format1));
    engine->shutdown();
    
    EXPECT_TRUE(engine->initialize(format2));
    engine->shutdown();
}

TEST_F(AudioEngineTest, BufferSizeConsistency) {
    engine->initialize(format);
    
    size_t buffer_size = engine->get_buffer_size();
    EXPECT_GT(buffer_size, 0);
    
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(engine->get_buffer_size(), buffer_size);
    }
    
    engine->shutdown();
}