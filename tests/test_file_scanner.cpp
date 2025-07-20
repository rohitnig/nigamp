#include <gtest/gtest.h>
#include "../src/file_scanner.cpp"
#include <filesystem>
#include <fstream>

class FileScannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        scanner = nigamp::create_file_scanner();
        
        test_dir = "test_audio_files";
        std::filesystem::create_directory(test_dir);
        
        create_test_file(test_dir + "/song1.mp3");
        create_test_file(test_dir + "/song2.wav");
        create_test_file(test_dir + "/not_audio.txt");
        create_test_file(test_dir + "/another_song.MP3");
    }
    
    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
    
    void create_test_file(const std::string& path) {
        std::ofstream file(path);
        file << "dummy content";
        file.close();
    }
    
    std::unique_ptr<nigamp::IFileScanner> scanner;
    std::string test_dir;
};

TEST_F(FileScannerTest, SupportedFormats) {
    EXPECT_TRUE(scanner->is_supported_format("test.mp3"));
    EXPECT_TRUE(scanner->is_supported_format("test.MP3"));
    EXPECT_TRUE(scanner->is_supported_format("test.wav"));
    EXPECT_TRUE(scanner->is_supported_format("test.WAV"));
    
    EXPECT_FALSE(scanner->is_supported_format("test.txt"));
    EXPECT_FALSE(scanner->is_supported_format("test.doc"));
    EXPECT_FALSE(scanner->is_supported_format("test"));
}

TEST_F(FileScannerTest, ScanDirectory) {
    auto songs = scanner->scan_directory(test_dir);
    
    EXPECT_EQ(songs.size(), 3);
    
    std::vector<std::string> found_files;
    for (const auto& song : songs) {
        std::string filename = std::filesystem::path(song.file_path).filename().string();
        found_files.push_back(filename);
    }
    
    EXPECT_TRUE(std::find(found_files.begin(), found_files.end(), "song1.mp3") != found_files.end());
    EXPECT_TRUE(std::find(found_files.begin(), found_files.end(), "song2.wav") != found_files.end());
    EXPECT_TRUE(std::find(found_files.begin(), found_files.end(), "another_song.MP3") != found_files.end());
    
    EXPECT_TRUE(std::find(found_files.begin(), found_files.end(), "not_audio.txt") == found_files.end());
}

TEST_F(FileScannerTest, EmptyDirectory) {
    std::string empty_dir = "empty_test_dir";
    std::filesystem::create_directory(empty_dir);
    
    auto songs = scanner->scan_directory(empty_dir);
    EXPECT_TRUE(songs.empty());
    
    std::filesystem::remove_all(empty_dir);
}

TEST_F(FileScannerTest, NonExistentDirectory) {
    auto songs = scanner->scan_directory("non_existent_directory");
    EXPECT_TRUE(songs.empty());
}

TEST_F(FileScannerTest, SongMetadata) {
    auto songs = scanner->scan_directory(test_dir);
    ASSERT_FALSE(songs.empty());
    
    const auto& song = songs[0];
    EXPECT_FALSE(song.file_path.empty());
    EXPECT_FALSE(song.title.empty());
    EXPECT_EQ(song.artist, "Unknown Artist");
    EXPECT_GE(song.duration, 0.0);
}