#pragma once

#include "types.hpp"
#include <memory>
#include <string>

namespace nigamp {

class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;
    virtual bool open(const std::string& file_path) = 0;
    virtual bool decode(AudioBuffer& buffer, size_t max_samples) = 0;
    virtual void close() = 0;
    virtual AudioFormat get_format() const = 0;
    virtual double get_duration() const = 0;
    virtual bool seek(double seconds) = 0;
    virtual bool is_eof() const = 0;
};

class Mp3Decoder : public IAudioDecoder {
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

public:
    Mp3Decoder();
    ~Mp3Decoder() override;

    bool open(const std::string& file_path) override;
    bool decode(AudioBuffer& buffer, size_t max_samples) override;
    void close() override;
    AudioFormat get_format() const override;
    double get_duration() const override;
    bool seek(double seconds) override;
    bool is_eof() const override;
};

class WavDecoder : public IAudioDecoder {
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

public:
    WavDecoder();
    ~WavDecoder() override;

    bool open(const std::string& file_path) override;
    bool decode(AudioBuffer& buffer, size_t max_samples) override;
    void close() override;
    AudioFormat get_format() const override;
    double get_duration() const override;
    bool seek(double seconds) override;
    bool is_eof() const override;
};

std::unique_ptr<IAudioDecoder> create_decoder(const std::string& file_path);

}