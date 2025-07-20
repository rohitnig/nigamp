#include "file_scanner.hpp"
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace nigamp {

FileScanner::FileScanner() {
    m_supported_extensions = {".mp3", ".wav"};
}

SongList FileScanner::scan_directory(const std::string& directory_path) {
    SongList songs;
    
    try {
        // Use recursive_directory_iterator for recursive search
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory_path)) {
            if (entry.is_regular_file()) {
                std::string file_path = entry.path().string();
                if (is_supported_format(file_path)) {
                    Song song = create_song_from_file(file_path);
                    songs.push_back(song);
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        // Skip inaccessible directories and continue
    }
    
    std::sort(songs.begin(), songs.end(), 
              [](const Song& a, const Song& b) {
                  return a.file_path < b.file_path;
              });
    
    return songs;
}

bool FileScanner::is_supported_format(const std::string& file_path) {
    std::string extension = get_file_extension(file_path);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    return std::find(m_supported_extensions.begin(), m_supported_extensions.end(), extension) 
           != m_supported_extensions.end();
}

Song FileScanner::create_song_from_file(const std::string& file_path) {
    Song song;
    song.file_path = file_path;
    song.title = extract_title_from_filename(file_path);
    song.artist = "Unknown Artist";
    song.duration = 0.0;
    
    return song;
}

std::string FileScanner::get_file_extension(const std::string& file_path) {
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        return file_path.substr(dot_pos);
    }
    return "";
}

std::string FileScanner::extract_title_from_filename(const std::string& file_path) {
    std::filesystem::path path(file_path);
    std::string filename = path.stem().string();
    
    if (filename.empty()) {
        return "Unknown Title";
    }
    
    std::replace(filename.begin(), filename.end(), '_', ' ');
    std::replace(filename.begin(), filename.end(), '-', ' ');
    
    return filename;
}

std::unique_ptr<IFileScanner> create_file_scanner() {
    return std::make_unique<FileScanner>();
}

}