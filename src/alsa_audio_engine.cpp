#include "audio_engine.hpp"
#include <alsa/asoundlib.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <string>
#include <deque>
#include <iostream>
#include <chrono>

namespace nigamp {

struct AlsaAudioEngine::Impl {
    snd_pcm_t* pcm_handle = nullptr;
    
    AudioFormat format;
    size_t buffer_size = 0;
    size_t period_size = 0;
    
    std::atomic<bool> is_playing{false};
    std::atomic<bool> is_paused{false};
    std::atomic<float> volume{1.0f};
    
    std::thread playback_thread;
    std::atomic<bool> should_stop{false};
    std::mutex buffer_mutex;

    std::deque<int16_t> pending_samples;
    
    // Callback-based completion detection
    CompletionCallback completion_callback;
    std::atomic<bool> eof_signaled{false};
    std::atomic<bool> callback_fired{false};
    std::mutex callback_mutex;
    size_t total_samples_processed = 0;
    std::chrono::steady_clock::time_point start_time;
    
    // Time-based completion tracking
    std::chrono::steady_clock::time_point last_audio_written_time;
    std::chrono::milliseconds estimated_remaining_ms{0};
    
    bool open_pcm() {
        int err = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
        if (err < 0) {
            std::cerr << "ALSA: Cannot open audio device: " << snd_strerror(err) << std::endl;
            return false;
        }
        return true;
    }
    
    bool set_hw_params() {
        snd_pcm_hw_params_t* hw_params;
        snd_pcm_hw_params_alloca(&hw_params);
        
        int err = snd_pcm_hw_params_any(pcm_handle, hw_params);
        if (err < 0) {
            std::cerr << "ALSA: Cannot initialize hardware parameters: " << snd_strerror(err) << std::endl;
            return false;
        }
        
        err = snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
        if (err < 0) {
            std::cerr << "ALSA: Cannot set access type: " << snd_strerror(err) << std::endl;
            return false;
        }
        
        snd_pcm_format_t pcm_format = SND_PCM_FORMAT_S16_LE;
        err = snd_pcm_hw_params_set_format(pcm_handle, hw_params, pcm_format);
        if (err < 0) {
            std::cerr << "ALSA: Cannot set sample format: " << snd_strerror(err) << std::endl;
            return false;
        }
        
        unsigned int channels = format.channels;
        err = snd_pcm_hw_params_set_channels(pcm_handle, hw_params, channels);
        if (err < 0) {
            std::cerr << "ALSA: Cannot set channel count: " << snd_strerror(err) << std::endl;
            return false;
        }
        
        unsigned int sample_rate = format.sample_rate;
        err = snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &sample_rate, 0);
        if (err < 0) {
            std::cerr << "ALSA: Cannot set sample rate: " << snd_strerror(err) << std::endl;
            return false;
        }
        
        // Set buffer size (2 seconds of audio)
        buffer_size = format.sample_rate * 2;
        snd_pcm_uframes_t buffer_frames = buffer_size;
        err = snd_pcm_hw_params_set_buffer_size_near(pcm_handle, hw_params, &buffer_frames);
        if (err < 0) {
            std::cerr << "ALSA: Cannot set buffer size: " << snd_strerror(err) << std::endl;
            return false;
        }
        buffer_size = buffer_frames;
        
        // Set period size (50ms of audio for low latency)
        period_size = format.sample_rate / 20; // 50ms
        snd_pcm_uframes_t period_frames = period_size;
        err = snd_pcm_hw_params_set_period_size_near(pcm_handle, hw_params, &period_frames, 0);
        if (err < 0) {
            std::cerr << "ALSA: Cannot set period size: " << snd_strerror(err) << std::endl;
            return false;
        }
        period_size = period_frames;
        
        err = snd_pcm_hw_params(pcm_handle, hw_params);
        if (err < 0) {
            std::cerr << "ALSA: Cannot set hardware parameters: " << snd_strerror(err) << std::endl;
            return false;
        }
        
        return true;
    }
    
    void check_completion() {
        if (eof_signaled.load() && pending_samples.empty()) {
            bool time_elapsed = is_audio_playback_complete_by_time();
            
            if (time_elapsed) {
                fire_completion_callback(AudioEngineError::SUCCESS);
            }
        }
    }
    
    bool is_audio_playback_complete_by_time() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_audio_written_time);
        return elapsed >= estimated_remaining_ms;
    }
    
    void fire_completion_callback(AudioEngineError error_code) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (!callback_fired.exchange(true) && completion_callback) {
            try {
                auto completion_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time);
                    
                CompletionResult result;
                result.error_code = error_code;
                result.error_message = get_error_description(error_code);
                result.completion_time = completion_time;
                result.samples_processed = total_samples_processed;
                
                completion_callback(result);
            } catch (...) {
                // Log exception but don't crash audio thread
            }
        }
    }
    
    std::string get_error_description(AudioEngineError error_code) {
        switch (error_code) {
            case AudioEngineError::SUCCESS: return "Playback completed successfully";
            case AudioEngineError::CALLBACK_EXCEPTION: return "Exception in completion callback";
            case AudioEngineError::BUFFER_UNDERRUN: return "Audio buffer underrun occurred";
            case AudioEngineError::THREADING_ERROR: return "Threading synchronization error";
            case AudioEngineError::DIRECTSOUND_FAILURE: return "ALSA operation failed";
            case AudioEngineError::CALLBACK_TIMEOUT: return "Completion callback timeout";
            default: return "Unknown error";
        }
    }
    
    void playback_loop() {
        while (!should_stop) {
            if (is_playing && !is_paused && pcm_handle) {
                update_buffer();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    void update_buffer() {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        
        if (pending_samples.empty()) {
            check_completion();
            return;
        }
        
        // Get available space in ALSA buffer
        snd_pcm_sframes_t avail = snd_pcm_avail(pcm_handle);
        if (avail < 0) {
            // Handle underrun
            if (avail == -EPIPE) {
                snd_pcm_prepare(pcm_handle);
            }
            return;
        }
        
        // Write up to period_size samples
        size_t samples_to_write = std::min(
            static_cast<size_t>(avail),
            std::min(pending_samples.size(), period_size * format.channels)
        );
        
        if (samples_to_write == 0) {
            return;
        }
        
        // Prepare buffer for writing
        std::vector<int16_t> write_buffer(samples_to_write);
        for (size_t i = 0; i < samples_to_write; ++i) {
            int16_t sample = pending_samples[i];
            // Apply volume
            sample = static_cast<int16_t>(sample * volume.load());
            write_buffer[i] = sample;
        }
        
        // Write to ALSA
        snd_pcm_sframes_t frames_written = snd_pcm_writei(
            pcm_handle, 
            write_buffer.data(), 
            samples_to_write / format.channels
        );
        
        if (frames_written < 0) {
            // Handle errors
            if (frames_written == -EPIPE) {
                snd_pcm_prepare(pcm_handle);
            } else if (frames_written == -ESTRPIPE) {
                snd_pcm_resume(pcm_handle);
            }
            return;
        }
        
        size_t samples_written = frames_written * format.channels;
        total_samples_processed += samples_written;
        
        pending_samples.erase(
            pending_samples.begin(), 
            pending_samples.begin() + samples_written
        );
        
        // Update timing-based completion tracking
        last_audio_written_time = std::chrono::steady_clock::now();
        size_t buffer_samples = buffer_size * format.channels;
        estimated_remaining_ms = std::chrono::milliseconds(
            (buffer_samples * 1000) / format.sample_rate);
    }
};

AlsaAudioEngine::AlsaAudioEngine() : m_impl(std::make_unique<Impl>()) {}

AlsaAudioEngine::~AlsaAudioEngine() {
    shutdown();
}

bool AlsaAudioEngine::initialize(const AudioFormat& fmt) {
    m_impl->format = fmt;
    
    if (!m_impl->open_pcm()) {
        return false;
    }
    
    if (!m_impl->set_hw_params()) {
        snd_pcm_close(m_impl->pcm_handle);
        m_impl->pcm_handle = nullptr;
        return false;
    }
    
    return true;
}

bool AlsaAudioEngine::start() {
    if (!m_impl->pcm_handle) {
        return false;
    }
    
    int err = snd_pcm_prepare(m_impl->pcm_handle);
    if (err < 0) {
        std::cerr << "ALSA: Cannot prepare PCM: " << snd_strerror(err) << std::endl;
        return false;
    }
    
    // Reset state for new playback
    m_impl->eof_signaled = false;
    m_impl->callback_fired = false;
    m_impl->total_samples_processed = 0;
    m_impl->start_time = std::chrono::steady_clock::now();
    
    m_impl->is_playing = true;
    m_impl->should_stop = false;
    m_impl->playback_thread = std::thread(&Impl::playback_loop, m_impl.get());
    
    return true;
}

bool AlsaAudioEngine::stop() {
    if (!m_impl->pcm_handle) {
        return false;
    }
    
    // Signal thread to stop FIRST
    m_impl->is_playing = false;
    m_impl->should_stop = true;
    
    // Wait for playback thread to finish
    if (m_impl->playback_thread.joinable()) {
        m_impl->playback_thread.join();
    }
    
    // Stop ALSA
    snd_pcm_drop(m_impl->pcm_handle);
    
    // Clear data structures
    {
        std::lock_guard<std::mutex> lock(m_impl->buffer_mutex);
        m_impl->pending_samples.clear();
    }
    
    // Clear callback
    {
        std::lock_guard<std::mutex> lock(m_impl->callback_mutex);
        m_impl->completion_callback = nullptr;
    }
    
    return true;
}

bool AlsaAudioEngine::pause() {
    if (m_impl->pcm_handle) {
        snd_pcm_pause(m_impl->pcm_handle, 1);
    }
    m_impl->is_paused = true;
    return true;
}

bool AlsaAudioEngine::resume() {
    if (m_impl->pcm_handle) {
        snd_pcm_pause(m_impl->pcm_handle, 0);
    }
    m_impl->is_paused = false;
    return true;
}

void AlsaAudioEngine::shutdown() {
    stop();
    
    if (m_impl->pcm_handle) {
        snd_pcm_close(m_impl->pcm_handle);
        m_impl->pcm_handle = nullptr;
    }
}

bool AlsaAudioEngine::write_samples(const AudioBuffer& buffer) {
    std::lock_guard<std::mutex> lock(m_impl->buffer_mutex);
    m_impl->pending_samples.insert(
        m_impl->pending_samples.end(), 
        buffer.begin(), 
        buffer.end()
    );
    return true;
}

size_t AlsaAudioEngine::get_buffer_size() const {
    return m_impl->buffer_size;
}

bool AlsaAudioEngine::is_playing() const {
    return m_impl->is_playing && !m_impl->is_paused;
}

void AlsaAudioEngine::set_volume(float volume) {
    m_impl->volume = std::clamp(volume, 0.0f, 1.0f);
}

float AlsaAudioEngine::get_volume() const {
    return m_impl->volume;
}

void AlsaAudioEngine::set_completion_callback(CompletionCallback callback) {
    std::lock_guard<std::mutex> lock(m_impl->callback_mutex);
    m_impl->completion_callback = callback;
}

void AlsaAudioEngine::signal_eof() {
    m_impl->eof_signaled = true;
    
    // Check completion immediately in case buffers are already empty
    std::lock_guard<std::mutex> lock(m_impl->buffer_mutex);
    m_impl->check_completion();
}

size_t AlsaAudioEngine::get_buffered_samples() const {
    std::lock_guard<std::mutex> lock(m_impl->buffer_mutex);
    return m_impl->pending_samples.size();
}

std::unique_ptr<IAudioEngine> create_audio_engine() {
    return std::make_unique<AlsaAudioEngine>();
}

}

