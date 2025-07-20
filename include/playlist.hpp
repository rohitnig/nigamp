#pragma once

#include "types.hpp"
#include <memory>
#include <random>

namespace nigamp {

class IPlaylist {
public:
    virtual ~IPlaylist() = default;
    virtual void add_song(const Song& song) = 0;
    virtual void clear() = 0;
    virtual const Song* current() const = 0;
    virtual const Song* next() = 0;
    virtual const Song* previous() = 0;
    virtual bool has_next() const = 0;
    virtual bool has_previous() const = 0;
    virtual size_t size() const = 0;
    virtual bool empty() const = 0;
    virtual void shuffle() = 0;
    virtual void reset() = 0;
};

class ShufflePlaylist : public IPlaylist {
private:
    SongList m_songs;
    SongList m_shuffled_songs;
    size_t m_current_index;
    std::mt19937 m_random_engine;
    bool m_is_shuffled;

public:
    ShufflePlaylist();
    ~ShufflePlaylist() override = default;

    void add_song(const Song& song) override;
    void clear() override;
    const Song* current() const override;
    const Song* next() override;
    const Song* previous() override;
    bool has_next() const override;
    bool has_previous() const override;
    size_t size() const override;
    bool empty() const override;
    void shuffle() override;
    void reset() override;

private:
    void fisher_yates_shuffle();
};

std::unique_ptr<IPlaylist> create_playlist();

}