#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace nigamp {

enum class PlaybackState {
    STOPPED,
    PLAYING,
    PAUSED
};

struct AudioFormat {
    int sample_rate{44100};
    int channels{2};
    int bits_per_sample{16};
};

struct Song {
    std::string file_path;
    std::string title;
    std::string artist;
    double duration{0.0};
};

using AudioBuffer = std::vector<int16_t>;
using SongList = std::vector<Song>;
using PlaybackCallback = std::function<void(PlaybackState)>;

}