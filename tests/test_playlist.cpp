#include <gtest/gtest.h>
#include "../src/playlist.cpp"

class PlaylistTest : public ::testing::Test {
protected:
    void SetUp() override {
        playlist = nigamp::create_playlist();
        
        song1.file_path = "test1.mp3";
        song1.title = "Song 1";
        song1.artist = "Artist 1";
        song1.duration = 180.0;
        
        song2.file_path = "test2.mp3";
        song2.title = "Song 2";
        song2.artist = "Artist 2";
        song2.duration = 200.0;
        
        song3.file_path = "test3.mp3";
        song3.title = "Song 3";
        song3.artist = "Artist 3";
        song3.duration = 220.0;
    }
    
    std::unique_ptr<nigamp::IPlaylist> playlist;
    nigamp::Song song1, song2, song3;
};

TEST_F(PlaylistTest, InitialState) {
    EXPECT_TRUE(playlist->empty());
    EXPECT_EQ(playlist->size(), 0);
    EXPECT_EQ(playlist->current(), nullptr);
    EXPECT_FALSE(playlist->has_next());
    EXPECT_FALSE(playlist->has_previous());
}

TEST_F(PlaylistTest, AddSongs) {
    playlist->add_song(song1);
    EXPECT_FALSE(playlist->empty());
    EXPECT_EQ(playlist->size(), 1);
    
    playlist->add_song(song2);
    playlist->add_song(song3);
    EXPECT_EQ(playlist->size(), 3);
}

TEST_F(PlaylistTest, CurrentSong) {
    playlist->add_song(song1);
    playlist->add_song(song2);
    
    const auto* current = playlist->current();
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->file_path, song1.file_path);
}

TEST_F(PlaylistTest, Navigation) {
    playlist->add_song(song1);
    playlist->add_song(song2);
    playlist->add_song(song3);
    
    EXPECT_TRUE(playlist->has_next());
    EXPECT_FALSE(playlist->has_previous());
    
    const auto* next = playlist->next();
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->file_path, song2.file_path);
    
    EXPECT_TRUE(playlist->has_previous());
    EXPECT_TRUE(playlist->has_next());
    
    const auto* prev = playlist->previous();
    ASSERT_NE(prev, nullptr);
    EXPECT_EQ(prev->file_path, song1.file_path);
}

TEST_F(PlaylistTest, Shuffle) {
    for (int i = 0; i < 10; ++i) {
        nigamp::Song song;
        song.file_path = "test" + std::to_string(i) + ".mp3";
        song.title = "Song " + std::to_string(i);
        playlist->add_song(song);
    }
    
    std::vector<std::string> original_order;
    playlist->reset();
    // Collect songs by iterating through the playlist size, not until next() fails
    for (size_t i = 0; i < playlist->size(); ++i) {
        const auto* song = playlist->current();
        ASSERT_NE(song, nullptr);
        original_order.push_back(song->file_path);
        if (i < playlist->size() - 1) {  // Don't call next() on the last iteration
            playlist->next();
        }
    }
    
    playlist->shuffle();
    
    std::vector<std::string> shuffled_order;
    // Same approach: iterate by size, not until next() fails
    for (size_t i = 0; i < playlist->size(); ++i) {
        const auto* song = playlist->current();
        ASSERT_NE(song, nullptr);
        shuffled_order.push_back(song->file_path);
        if (i < playlist->size() - 1) {  // Don't call next() on the last iteration
            playlist->next();
        }
    }
    
    EXPECT_EQ(original_order.size(), shuffled_order.size());
    EXPECT_EQ(original_order.size(), 10);
    
    // Verify all original songs are still present after shuffle
    std::sort(original_order.begin(), original_order.end());
    std::sort(shuffled_order.begin(), shuffled_order.end());
    EXPECT_EQ(original_order, shuffled_order);
}

TEST_F(PlaylistTest, Clear) {
    playlist->add_song(song1);
    playlist->add_song(song2);
    
    EXPECT_FALSE(playlist->empty());
    
    playlist->clear();
    
    EXPECT_TRUE(playlist->empty());
    EXPECT_EQ(playlist->size(), 0);
    EXPECT_EQ(playlist->current(), nullptr);
}