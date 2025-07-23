#include "audio_engine.hpp"
#include <windows.h>
#include <dsound.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <string>

namespace nigamp {

struct DirectSoundEngine::Impl {
    LPDIRECTSOUND8 ds_device = nullptr;
    LPDIRECTSOUNDBUFFER primary_buffer = nullptr;
    LPDIRECTSOUNDBUFFER secondary_buffer = nullptr;
    HWND window_handle = nullptr;
    
    AudioFormat format;
    size_t buffer_size = 0;
    size_t buffer_bytes = 0;
    
    std::atomic<bool> is_playing{false};
    std::atomic<bool> is_paused{false};
    std::atomic<float> volume{1.0f};
    
    std::thread playback_thread;
    std::atomic<bool> should_stop{false};
    std::mutex buffer_mutex;
    
    AudioBuffer pending_samples;
    size_t write_cursor = 0;
    
    // New callback-based completion detection
    CompletionCallback completion_callback;
    std::atomic<bool> eof_signaled{false};
    std::atomic<bool> callback_fired{false};
    std::mutex callback_mutex;
    size_t total_samples_processed = 0;
    std::chrono::steady_clock::time_point start_time;
    
    bool create_window() {
        WNDCLASS wc = {};
        wc.lpfnWndProc = DefWindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = "NigampAudioWindow";
        RegisterClass(&wc);
        
        window_handle = CreateWindowEx(
            0, "NigampAudioWindow", "Nigamp Audio",
            0, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
            
        return window_handle != nullptr;
    }
    
    bool create_primary_buffer() {
        DSBUFFERDESC buffer_desc = {};
        buffer_desc.dwSize = sizeof(DSBUFFERDESC);
        buffer_desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME;
        buffer_desc.dwBufferBytes = 0;
        buffer_desc.lpwfxFormat = nullptr;
        
        HRESULT hr = ds_device->CreateSoundBuffer(&buffer_desc, &primary_buffer, nullptr);
        if (FAILED(hr)) {
            return false;
        }
        
        WAVEFORMATEX wave_format = {};
        wave_format.wFormatTag = WAVE_FORMAT_PCM;
        wave_format.nChannels = static_cast<WORD>(format.channels);
        wave_format.nSamplesPerSec = static_cast<DWORD>(format.sample_rate);
        wave_format.wBitsPerSample = static_cast<WORD>(format.bits_per_sample);
        wave_format.nBlockAlign = (wave_format.nChannels * wave_format.wBitsPerSample) / 8;
        wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
        wave_format.cbSize = 0;
        
        hr = primary_buffer->SetFormat(&wave_format);
        return SUCCEEDED(hr);
    }
    
    bool create_secondary_buffer() {
        buffer_size = format.sample_rate * 2;
        buffer_bytes = buffer_size * format.channels * (format.bits_per_sample / 8);
        
        WAVEFORMATEX wave_format = {};
        wave_format.wFormatTag = WAVE_FORMAT_PCM;
        wave_format.nChannels = static_cast<WORD>(format.channels);
        wave_format.nSamplesPerSec = static_cast<DWORD>(format.sample_rate);
        wave_format.wBitsPerSample = static_cast<WORD>(format.bits_per_sample);
        wave_format.nBlockAlign = (wave_format.nChannels * wave_format.wBitsPerSample) / 8;
        wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
        wave_format.cbSize = 0;
        
        DSBUFFERDESC buffer_desc = {};
        buffer_desc.dwSize = sizeof(DSBUFFERDESC);
        buffer_desc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLVOLUME;
        buffer_desc.dwBufferBytes = static_cast<DWORD>(buffer_bytes);
        buffer_desc.lpwfxFormat = &wave_format;
        
        HRESULT hr = ds_device->CreateSoundBuffer(&buffer_desc, &secondary_buffer, nullptr);
        return SUCCEEDED(hr);
    }
    
    void check_completion() {
        if (eof_signaled.load() && pending_samples.empty()) {
            fire_completion_callback(AudioEngineError::SUCCESS);
        }
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
            case AudioEngineError::DIRECTSOUND_FAILURE: return "DirectSound operation failed";
            case AudioEngineError::CALLBACK_TIMEOUT: return "Completion callback timeout";
            default: return "Unknown error";
        }
    }
    
    void playback_loop() {
        while (!should_stop) {
            if (is_playing && !is_paused) {
                update_buffer();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    void update_buffer() {
        DWORD play_cursor, write_cursor_pos;
        HRESULT hr = secondary_buffer->GetCurrentPosition(&play_cursor, &write_cursor_pos);
        if (FAILED(hr)) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(buffer_mutex);
        if (pending_samples.empty()) {
            // Check for completion when buffers are empty
            check_completion();
            return;
        }
        
        DWORD bytes_to_write = 0;
        if (write_cursor > play_cursor) {
            bytes_to_write = static_cast<DWORD>(buffer_bytes) - write_cursor + play_cursor;
        } else {
            bytes_to_write = play_cursor - write_cursor;
        }
        
        if (bytes_to_write < 1024) {
            return;
        }
        
        LPVOID audio_ptr1, audio_ptr2;
        DWORD audio_bytes1, audio_bytes2;
        
        hr = secondary_buffer->Lock(write_cursor, bytes_to_write, 
                                   &audio_ptr1, &audio_bytes1,
                                   &audio_ptr2, &audio_bytes2, 0);
        if (FAILED(hr)) {
            return;
        }
        
        size_t samples_to_copy = std::min(pending_samples.size(), 
                                        (audio_bytes1 + audio_bytes2) / sizeof(int16_t));
        
        if (audio_ptr1 && audio_bytes1 > 0) {
            size_t samples1 = std::min(samples_to_copy, audio_bytes1 / sizeof(int16_t));
            memcpy(audio_ptr1, pending_samples.data(), samples1 * sizeof(int16_t));
            samples_to_copy -= samples1;
        }
        
        if (audio_ptr2 && audio_bytes2 > 0 && samples_to_copy > 0) {
            size_t offset = audio_bytes1 / sizeof(int16_t);
            memcpy(audio_ptr2, pending_samples.data() + offset, samples_to_copy * sizeof(int16_t));
        }
        
        secondary_buffer->Unlock(audio_ptr1, audio_bytes1, audio_ptr2, audio_bytes2);
        
        size_t samples_written = (audio_bytes1 + audio_bytes2) / sizeof(int16_t);
        total_samples_processed += samples_written;
        
        pending_samples.erase(pending_samples.begin(), 
                            pending_samples.begin() + samples_written);
        
        write_cursor = (write_cursor + bytes_to_write) % buffer_bytes;
    }
};

DirectSoundEngine::DirectSoundEngine() : m_impl(std::make_unique<Impl>()) {}

DirectSoundEngine::~DirectSoundEngine() {
    shutdown();
}

bool DirectSoundEngine::initialize(const AudioFormat& format) {
    m_impl->format = format;
    
    if (!m_impl->create_window()) {
        return false;
    }
    
    HRESULT hr = DirectSoundCreate8(nullptr, &m_impl->ds_device, nullptr);
    if (FAILED(hr)) {
        return false;
    }
    
    hr = m_impl->ds_device->SetCooperativeLevel(m_impl->window_handle, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        return false;
    }
    
    if (!m_impl->create_primary_buffer() || !m_impl->create_secondary_buffer()) {
        return false;
    }
    
    return true;
}

bool DirectSoundEngine::start() {
    if (!m_impl->secondary_buffer) {
        return false;
    }
    
    HRESULT hr = m_impl->secondary_buffer->Play(0, 0, DSBPLAY_LOOPING);
    if (FAILED(hr)) {
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

bool DirectSoundEngine::stop() {
    if (!m_impl->secondary_buffer) {
        return false;
    }
    
    m_impl->is_playing = false;
    m_impl->should_stop = true;
    
    // Clear pending audio samples immediately for instant stop
    {
        std::lock_guard<std::mutex> lock(m_impl->buffer_mutex);
        m_impl->pending_samples.clear();
    }
    
    // Clear callback to prevent firing during shutdown
    {
        std::lock_guard<std::mutex> lock(m_impl->callback_mutex);
        m_impl->completion_callback = nullptr;
    }
    
    if (m_impl->playback_thread.joinable()) {
        m_impl->playback_thread.join();
    }
    
    HRESULT hr = m_impl->secondary_buffer->Stop();
    
    // Reset write cursor position for clean start on next play
    m_impl->write_cursor = 0;
    
    // Optionally reset playback position to beginning for immediate silence
    m_impl->secondary_buffer->SetCurrentPosition(0);
    
    return SUCCEEDED(hr);
}

bool DirectSoundEngine::pause() {
    m_impl->is_paused = true;
    return true;
}

bool DirectSoundEngine::resume() {
    m_impl->is_paused = false;
    return true;
}

void DirectSoundEngine::shutdown() {
    stop();
    
    if (m_impl->secondary_buffer) {
        m_impl->secondary_buffer->Release();
        m_impl->secondary_buffer = nullptr;
    }
    
    if (m_impl->primary_buffer) {
        m_impl->primary_buffer->Release();
        m_impl->primary_buffer = nullptr;
    }
    
    if (m_impl->ds_device) {
        m_impl->ds_device->Release();
        m_impl->ds_device = nullptr;
    }
    
    if (m_impl->window_handle) {
        DestroyWindow(m_impl->window_handle);
        m_impl->window_handle = nullptr;
    }
}

bool DirectSoundEngine::write_samples(const AudioBuffer& buffer) {
    std::lock_guard<std::mutex> lock(m_impl->buffer_mutex);
    m_impl->pending_samples.insert(m_impl->pending_samples.end(), buffer.begin(), buffer.end());
    return true;
}

size_t DirectSoundEngine::get_buffer_size() const {
    return m_impl->buffer_size;
}

bool DirectSoundEngine::is_playing() const {
    return m_impl->is_playing && !m_impl->is_paused;
}

void DirectSoundEngine::set_volume(float volume) {
    m_impl->volume = std::clamp(volume, 0.0f, 1.0f);
    
    if (m_impl->secondary_buffer) {
        LONG ds_volume = static_cast<LONG>((1.0f - m_impl->volume) * -10000);
        m_impl->secondary_buffer->SetVolume(ds_volume);
    }
}

float DirectSoundEngine::get_volume() const {
    return m_impl->volume;
}

void DirectSoundEngine::set_completion_callback(CompletionCallback callback) {
    std::lock_guard<std::mutex> lock(m_impl->callback_mutex);
    m_impl->completion_callback = callback;
}

void DirectSoundEngine::signal_eof() {
    m_impl->eof_signaled = true;
    
    // Check completion immediately in case buffers are already empty
    std::lock_guard<std::mutex> lock(m_impl->buffer_mutex);
    m_impl->check_completion();
}

size_t DirectSoundEngine::get_buffered_samples() const {
    std::lock_guard<std::mutex> lock(m_impl->buffer_mutex);
    return m_impl->pending_samples.size();
}

std::unique_ptr<IAudioEngine> create_audio_engine() {
    return std::make_unique<DirectSoundEngine>();
}

}