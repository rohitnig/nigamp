#include "playlist.hpp"
#include <algorithm>
#include <random>
#include <chrono>
#include <iostream>

namespace nigamp {

ShufflePlaylist::ShufflePlaylist() 
    : m_current_index(0)
    , m_is_shuffled(false) {
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    m_random_engine.seed(static_cast<std::mt19937::result_type>(seed));
}

void ShufflePlaylist::add_song(const Song& song) {
    m_songs.push_back(song);
    if (m_is_shuffled) {
        m_shuffled_songs.push_back(song);
        
        if (m_shuffled_songs.size() > 1) {
            std::uniform_int_distribution<size_t> dist(0, m_shuffled_songs.size() - 1);
            size_t swap_index = dist(m_random_engine);
            std::swap(m_shuffled_songs.back(), m_shuffled_songs[swap_index]);
        }
    }
}

void ShufflePlaylist::clear() {
    m_songs.clear();
    m_shuffled_songs.clear();
    m_current_index = 0;
    m_is_shuffled = false;
}

const Song* ShufflePlaylist::current() const {
    if (empty()) {
        return nullptr;
    }
    
    const auto& playlist = m_is_shuffled ? m_shuffled_songs : m_songs;
    if (m_current_index >= playlist.size()) {
        return nullptr;
    }
    
    return &playlist[m_current_index];
}

const Song* ShufflePlaylist::next() {
    if (empty()) {
        std::cout << "Playlist is empty, cannot get next song." << std::endl;
        return nullptr;
    }
    
    const auto& playlist = m_is_shuffled ? m_shuffled_songs : m_songs;
    
    if (m_current_index + 1 < playlist.size()) {
        std::cout << "[DEBUG] next() current index: " << m_current_index << std::endl;
        std::cout << "[DEBUG] next() playlist size: " << playlist.size() << std::endl;
        ++m_current_index;
        return &playlist[m_current_index];
    } else if (playlist.size() == 1) {
        std::cout << "[DEBUG] next() single song, returning same song" << std::endl;
        // For single song, restart the same song
        return &playlist[m_current_index];
    } else {
        // Loop back to first song
        std::cout << "[DEBUG] next() looping to first song" << std::endl;
        m_current_index = 0;
        return &playlist[m_current_index];
    }
}

const Song* ShufflePlaylist::previous() {
    if (empty()) {
        return nullptr;
    }
    
    const auto& playlist = m_is_shuffled ? m_shuffled_songs : m_songs;
    
    if (m_current_index > 0) {
        --m_current_index;
        return &playlist[m_current_index];
    } else if (playlist.size() == 1) {
        // For single song, restart the same song
        return &playlist[m_current_index];
    } else {
        // Loop to last song
        m_current_index = playlist.size() - 1;
        return &playlist[m_current_index];
    }
}

bool ShufflePlaylist::has_next() const {
    if (empty()) {
        return false;
    }
    
    const auto& playlist = m_is_shuffled ? m_shuffled_songs : m_songs;
    return m_current_index + 1 < playlist.size();
}

bool ShufflePlaylist::has_previous() const {
    return !empty() && m_current_index > 0;
}

size_t ShufflePlaylist::size() const {
    return m_songs.size();
}

bool ShufflePlaylist::empty() const {
    return m_songs.empty();
}

void ShufflePlaylist::shuffle() {
    if (m_songs.empty()) {
        return;
    }
    
    m_shuffled_songs = m_songs;
    fisher_yates_shuffle();
    m_current_index = 0;
    m_is_shuffled = true;
}

void ShufflePlaylist::reset() {
    m_current_index = 0;
    m_is_shuffled = false;
    m_shuffled_songs.clear();
}

void ShufflePlaylist::fisher_yates_shuffle() {
    for (size_t i = m_shuffled_songs.size() - 1; i > 0; --i) {
        std::uniform_int_distribution<size_t> dist(0, i);
        size_t j = dist(m_random_engine);
        std::swap(m_shuffled_songs[i], m_shuffled_songs[j]);
    }
}

std::unique_ptr<IPlaylist> create_playlist() {
    return std::make_unique<ShufflePlaylist>();
}

}