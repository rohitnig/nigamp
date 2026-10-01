#pragma once

#include "hotkey_handler.hpp"
#include <memory>
#include <string>

namespace nigamp {

enum class PlaybackStatus {
    PLAYING,
    PAUSED,
    STOPPED
};

struct TrackInfo {
    std::string title;
    std::string file_path;
    double duration_seconds = 0.0;
};

// Desktop media-control integration (media keys, system media widgets).
// Commands from the desktop are delivered through the same HotkeyCallback as hotkeys.
class IMediaControls {
public:
    virtual ~IMediaControls() = default;
    // Returns false when the service is unavailable; the player works without it
    virtual bool initialize(HotkeyCallback callback) = 0;
    virtual void shutdown() = 0;
    virtual void set_track(const TrackInfo& track) = 0;
    virtual void set_playback_status(PlaybackStatus status) = 0;
    virtual void set_volume(double volume) = 0;
};

// Linux: MPRIS over the D-Bus session bus (src/mpris_media_controls.cpp)
class MprisMediaControls : public IMediaControls {
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

public:
    MprisMediaControls();
    ~MprisMediaControls() override;

    bool initialize(HotkeyCallback callback) override;
    void shutdown() override;
    void set_track(const TrackInfo& track) override;
    void set_playback_status(PlaybackStatus status) override;
    void set_volume(double volume) override;
};

// No-op fallback for Windows and Linux builds without libsystemd (src/null_media_controls.cpp)
class NullMediaControls : public IMediaControls {
public:
    bool initialize(HotkeyCallback) override { return false; }
    void shutdown() override {}
    void set_track(const TrackInfo&) override {}
    void set_playback_status(PlaybackStatus) override {}
    void set_volume(double) override {}
};

std::unique_ptr<IMediaControls> create_media_controls();

}
