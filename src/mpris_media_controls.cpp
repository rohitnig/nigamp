#include "media_controls.hpp"
#include <systemd/sd-bus.h>
#include <atomic>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

namespace nigamp {

namespace {

constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kRootInterface = "org.mpris.MediaPlayer2";
constexpr const char* kPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr const char* kBusName = "org.mpris.MediaPlayer2.nigamp";
constexpr const char* kNoTrackId = "/org/mpris/MediaPlayer2/TrackList/NoTrack";

const char* status_name(PlaybackStatus status) {
    switch (status) {
        case PlaybackStatus::PLAYING: return "Playing";
        case PlaybackStatus::PAUSED:  return "Paused";
        default:                      return "Stopped";
    }
}

// MPRIS xesam:url must be a URI; percent-encode everything outside the unreserved set
std::string file_uri(const std::string& path) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    std::string uri = "file://";
    for (unsigned char c : (ec ? path : absolute.lexically_normal().string())) {
        if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
            uri += static_cast<char>(c);
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            uri += buf;
        }
    }
    return uri;
}

}  // namespace

struct MprisMediaControls::Impl {
    sd_bus* bus = nullptr;
    sd_bus_slot* root_slot = nullptr;
    sd_bus_slot* player_slot = nullptr;
    std::thread bus_thread;
    std::atomic<bool> should_stop{false};
    HotkeyCallback callback;

    // sd-bus objects are not thread-safe: only bus_thread touches `bus`. Other threads
    // update this state and set `dirty`; bus_thread then emits PropertiesChanged.
    std::mutex state_mutex;
    TrackInfo track;
    bool has_track = false;
    uint64_t track_number = 0;
    PlaybackStatus status = PlaybackStatus::STOPPED;
    double volume = 1.0;
    std::atomic<bool> dirty{false};

    void dispatch(HotkeyAction action) {
        if (callback) {
            callback(action);
        }
    }

    PlaybackStatus current_status() {
        std::lock_guard<std::mutex> lock(state_mutex);
        return status;
    }

    void bus_loop() {
        while (!should_stop) {
            // Drain all queued messages, then block briefly so shutdown stays responsive
            while (sd_bus_process(bus, nullptr) > 0) {}

            if (dirty.exchange(false)) {
                sd_bus_emit_properties_changed(bus, kObjectPath, kPlayerInterface,
                                               "PlaybackStatus", "Metadata", "Volume", nullptr);
                sd_bus_flush(bus);
            }

            sd_bus_wait(bus, 100000 /* usec */);
        }
    }

    // ---- org.mpris.MediaPlayer2 ----

    static int method_noop(sd_bus_message* m, void*, sd_bus_error*) {
        return sd_bus_reply_method_return(m, "");
    }

    static int method_quit(sd_bus_message* m, void* userdata, sd_bus_error*) {
        static_cast<Impl*>(userdata)->dispatch(HotkeyAction::QUIT);
        return sd_bus_reply_method_return(m, "");
    }

    static int prop_true(sd_bus*, const char*, const char*, const char*,
                         sd_bus_message* reply, void*, sd_bus_error*) {
        return sd_bus_message_append(reply, "b", 1);
    }

    static int prop_false(sd_bus*, const char*, const char*, const char*,
                          sd_bus_message* reply, void*, sd_bus_error*) {
        return sd_bus_message_append(reply, "b", 0);
    }

    static int prop_identity(sd_bus*, const char*, const char*, const char*,
                             sd_bus_message* reply, void*, sd_bus_error*) {
        return sd_bus_message_append(reply, "s", "Nigamp");
    }

    static int prop_uri_schemes(sd_bus*, const char*, const char*, const char*,
                                sd_bus_message* reply, void*, sd_bus_error*) {
        return sd_bus_message_append(reply, "as", 1, "file");
    }

    static int prop_mime_types(sd_bus*, const char*, const char*, const char*,
                               sd_bus_message* reply, void*, sd_bus_error*) {
        return sd_bus_message_append(reply, "as", 3, "audio/mpeg", "audio/wav", "audio/x-wav");
    }

    // ---- org.mpris.MediaPlayer2.Player ----

    static int method_next(sd_bus_message* m, void* userdata, sd_bus_error*) {
        static_cast<Impl*>(userdata)->dispatch(HotkeyAction::NEXT_TRACK);
        return sd_bus_reply_method_return(m, "");
    }

    static int method_previous(sd_bus_message* m, void* userdata, sd_bus_error*) {
        static_cast<Impl*>(userdata)->dispatch(HotkeyAction::PREVIOUS_TRACK);
        return sd_bus_reply_method_return(m, "");
    }

    static int method_play_pause(sd_bus_message* m, void* userdata, sd_bus_error*) {
        static_cast<Impl*>(userdata)->dispatch(HotkeyAction::PAUSE_RESUME);
        return sd_bus_reply_method_return(m, "");
    }

    // The player only has a toggle, so Play/Pause/Stop toggle only when it changes state.
    // Stop maps to pause: the player has no stopped state.
    static int method_play(sd_bus_message* m, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        if (self->current_status() == PlaybackStatus::PAUSED) {
            self->dispatch(HotkeyAction::PAUSE_RESUME);
        }
        return sd_bus_reply_method_return(m, "");
    }

    static int method_pause(sd_bus_message* m, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        if (self->current_status() == PlaybackStatus::PLAYING) {
            self->dispatch(HotkeyAction::PAUSE_RESUME);
        }
        return sd_bus_reply_method_return(m, "");
    }

    static int prop_playback_status(sd_bus*, const char*, const char*, const char*,
                                    sd_bus_message* reply, void* userdata, sd_bus_error*) {
        return sd_bus_message_append(reply, "s", status_name(static_cast<Impl*>(userdata)->current_status()));
    }

    static int prop_volume(sd_bus*, const char*, const char*, const char*,
                           sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        std::lock_guard<std::mutex> lock(self->state_mutex);
        return sd_bus_message_append(reply, "d", self->volume);
    }

    static int prop_rate(sd_bus*, const char*, const char*, const char*,
                         sd_bus_message* reply, void*, sd_bus_error*) {
        return sd_bus_message_append(reply, "d", 1.0);
    }

    // Position isn't tracked; CanSeek=false tells clients not to rely on it
    static int prop_position(sd_bus*, const char*, const char*, const char*,
                             sd_bus_message* reply, void*, sd_bus_error*) {
        return sd_bus_message_append(reply, "x", static_cast<int64_t>(0));
    }

    template <typename... Args>
    static int append_metadata_entry(sd_bus_message* reply, const char* key, const char* type, Args... value) {
        int r = sd_bus_message_open_container(reply, 'e', "sv");
        if (r >= 0) r = sd_bus_message_append(reply, "s", key);
        if (r >= 0) r = sd_bus_message_open_container(reply, 'v', type);
        if (r >= 0) r = sd_bus_message_append(reply, type, value...);
        if (r >= 0) r = sd_bus_message_close_container(reply);
        if (r >= 0) r = sd_bus_message_close_container(reply);
        return r;
    }

    static int prop_metadata(sd_bus*, const char*, const char*, const char*,
                             sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        std::lock_guard<std::mutex> lock(self->state_mutex);

        int r = sd_bus_message_open_container(reply, 'a', "{sv}");
        if (r < 0) return r;

        if (!self->has_track) {
            r = append_metadata_entry(reply, "mpris:trackid", "o", kNoTrackId);
        } else {
            const std::string track_id = "/org/nigamp/track/" + std::to_string(self->track_number);
            const std::string url = file_uri(self->track.file_path);
            r = append_metadata_entry(reply, "mpris:trackid", "o", track_id.c_str());
            if (r >= 0) r = append_metadata_entry(reply, "xesam:title", "s", self->track.title.c_str());
            if (r >= 0) r = append_metadata_entry(reply, "xesam:url", "s", url.c_str());
            if (r >= 0 && self->track.duration_seconds > 0) {
                const int64_t length_us = static_cast<int64_t>(self->track.duration_seconds * 1e6);
                r = append_metadata_entry(reply, "mpris:length", "x", length_us);
            }
        }
        if (r < 0) return r;

        return sd_bus_message_close_container(reply);
    }

    static const sd_bus_vtable root_vtable[];
    static const sd_bus_vtable player_vtable[];
};

const sd_bus_vtable MprisMediaControls::Impl::root_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Raise", "", "", method_noop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Quit", "", "", method_quit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("CanQuit", "b", prop_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanRaise", "b", prop_false, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("HasTrackList", "b", prop_false, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Identity", "s", prop_identity, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedUriSchemes", "as", prop_uri_schemes, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedMimeTypes", "as", prop_mime_types, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END
};

const sd_bus_vtable MprisMediaControls::Impl::player_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Next", "", "", method_next, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Previous", "", "", method_previous, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Pause", "", "", method_pause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("PlayPause", "", "", method_play_pause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Stop", "", "", method_pause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Play", "", "", method_play, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Seek", "x", "", method_noop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPosition", "ox", "", method_noop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("OpenUri", "s", "", method_noop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("PlaybackStatus", "s", prop_playback_status, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Metadata", "a{sv}", prop_metadata, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Volume", "d", prop_volume, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Position", "x", prop_position, 0, 0),
    SD_BUS_PROPERTY("Rate", "d", prop_rate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("MinimumRate", "d", prop_rate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("MaximumRate", "d", prop_rate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanGoNext", "b", prop_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanGoPrevious", "b", prop_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanPlay", "b", prop_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanPause", "b", prop_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanSeek", "b", prop_false, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanControl", "b", prop_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END
};

MprisMediaControls::MprisMediaControls() : m_impl(std::make_unique<Impl>()) {}

MprisMediaControls::~MprisMediaControls() {
    shutdown();
}

bool MprisMediaControls::initialize(HotkeyCallback callback) {
    m_impl->callback = std::move(callback);

    if (sd_bus_open_user(&m_impl->bus) < 0) {
        return false;
    }

    int r = sd_bus_add_object_vtable(m_impl->bus, &m_impl->root_slot, kObjectPath, kRootInterface,
                                     Impl::root_vtable, m_impl.get());
    if (r >= 0) {
        r = sd_bus_add_object_vtable(m_impl->bus, &m_impl->player_slot, kObjectPath, kPlayerInterface,
                                     Impl::player_vtable, m_impl.get());
    }
    if (r >= 0) {
        r = sd_bus_request_name(m_impl->bus, kBusName, 0);
        if (r < 0) {
            // Another nigamp owns the name; MPRIS allows a unique per-instance suffix
            const std::string instance_name = std::string(kBusName) + ".instance" + std::to_string(getpid());
            r = sd_bus_request_name(m_impl->bus, instance_name.c_str(), 0);
        }
    }
    if (r < 0) {
        shutdown();
        return false;
    }

    m_impl->should_stop = false;
    m_impl->bus_thread = std::thread(&Impl::bus_loop, m_impl.get());
    return true;
}

void MprisMediaControls::shutdown() {
    m_impl->should_stop = true;
    if (m_impl->bus_thread.joinable()) {
        m_impl->bus_thread.join();
    }

    // Null the handles so a second shutdown() (e.g. from the destructor) is a no-op
    m_impl->player_slot = sd_bus_slot_unref(m_impl->player_slot);
    m_impl->root_slot = sd_bus_slot_unref(m_impl->root_slot);
    m_impl->bus = sd_bus_flush_close_unref(m_impl->bus);
}

void MprisMediaControls::set_track(const TrackInfo& track) {
    {
        std::lock_guard<std::mutex> lock(m_impl->state_mutex);
        m_impl->track = track;
        m_impl->has_track = true;
        ++m_impl->track_number;
    }
    m_impl->dirty = true;
}

void MprisMediaControls::set_playback_status(PlaybackStatus status) {
    {
        std::lock_guard<std::mutex> lock(m_impl->state_mutex);
        m_impl->status = status;
    }
    m_impl->dirty = true;
}

void MprisMediaControls::set_volume(double volume) {
    {
        std::lock_guard<std::mutex> lock(m_impl->state_mutex);
        m_impl->volume = volume;
    }
    m_impl->dirty = true;
}

std::unique_ptr<IMediaControls> create_media_controls() {
    return std::make_unique<MprisMediaControls>();
}

}
