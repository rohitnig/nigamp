#pragma once

#include "types.hpp"
#include <memory>
#include <functional>

namespace nigamp {

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;
    virtual bool initialize(const AudioFormat& format) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual void shutdown() = 0;
    virtual bool write_samples(const AudioBuffer& buffer) = 0;
    virtual size_t get_buffer_size() const = 0;
    virtual bool is_playing() const = 0;
    virtual void set_volume(float volume) = 0;
    virtual float get_volume() const = 0;
};

class DirectSoundEngine : public IAudioEngine {
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

public:
    DirectSoundEngine();
    ~DirectSoundEngine() override;

    bool initialize(const AudioFormat& format) override;
    bool start() override;
    bool stop() override;
    bool pause() override;
    bool resume() override;
    void shutdown() override;
    bool write_samples(const AudioBuffer& buffer) override;
    size_t get_buffer_size() const override;
    bool is_playing() const override;
    void set_volume(float volume) override;
    float get_volume() const override;
};

std::unique_ptr<IAudioEngine> create_audio_engine();

}