#pragma once

#include <filesystem>
#include <print>

#include <boost/asio.hpp>

// ALL CALLS TO FILEMANAGER MUST GO THROUGH THE DISK EXECUTOR ONLY

struct TorrentFile;

class FileManager {
public:

    FileManager(std::filesystem::path root, std::string_view torrent_name, std::vector<TorrentFile>& file_list, uint64_t total_size, uint64_t piece_length): standard_piece_length(piece_length) {
        savefile = std::string(torrent_name) + ".fastresume";            
        build_output_files(root, torrent_name, file_list, total_size);
    }

    ~FileManager() {
        std::println("Fm destroyed");
    }

    boost::asio::awaitable<void> write_piece(uint32_t piece, std::vector<unsigned char> data);
    boost::asio::awaitable<std::optional<std::vector<unsigned char>>> read_block(uint32_t piece, uint32_t begin, uint32_t length);
    std::optional<std::vector<uint32_t>> read_save_file();

private:

    struct OutputFile {
        std::filesystem::path path;
        uint64_t length, offset;
    };

    std::vector<OutputFile> _output_files;

    void build_output_files(std::filesystem::path root, std::string_view torrent_name, std::vector<TorrentFile>& file_list, uint64_t total_size);
    void mark_complete(uint32_t piece);

    uint64_t standard_piece_length;
    
    std::string savefile;
};