#pragma once

#include "media_controls.hpp"
#include <functional>
#include <memory>

namespace nigamp {

using VolumeCallback = std::function<void(double)>;

// Mini-player window. Receives the same track/status/volume updates as the
// desktop media controls, and sends commands back through HotkeyCallback.
//
// Threading: setters may be called from any thread (they only update shared
// state). initialize(), pump() and shutdown() must run on the main thread, and
// callbacks fire on the main thread from inside pump().
class IPlayerGui : public IMediaControls {
public:
    // Elapsed playing time and the total shown by the progress bar
    virtual void set_position(double played_seconds, double total_seconds) = 0;
    virtual void set_preview_mode(bool preview) = 0;
    // Absolute volume (0..1) chosen with the volume slider
    virtual void set_volume_callback(VolumeCallback callback) = 0;
    // Processes window events for up to timeout_seconds
    virtual void pump(double timeout_seconds) = 0;
};

// Returns nullptr when nigamp was built without GUI support
std::unique_ptr<IPlayerGui> create_player_gui();

}
