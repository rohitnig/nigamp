#pragma once

#include "types.hpp"
#include <string>
#include <vector>

namespace nigamp {

class IFileScanner {
public:
    virtual ~IFileScanner() = default;
    virtual SongList scan_directory(const std::string& directory_path) = 0;
    virtual bool is_supported_format(const std::string& file_path) = 0;
};

class FileScanner : public IFileScanner {
private:
    std::vector<std::string> m_supported_extensions;

public:
    FileScanner();
    ~FileScanner() override = default;

    SongList scan_directory(const std::string& directory_path) override;
    bool is_supported_format(const std::string& file_path) override;

private:
    Song create_song_from_file(const std::string& file_path);
    std::string get_file_extension(const std::string& file_path);
    std::string extract_title_from_filename(const std::string& file_path);
};

std::unique_ptr<IFileScanner> create_file_scanner();

}