#include "FileManager.hpp"
#include "MetadataParser.hpp"

#include <fstream>

#include <boost/asio.hpp>

// make a list of output files with offsets
void FileManager::build_output_files(std::filesystem::path root, std::string_view torrent_name, std::vector<TorrentFile>& file_list, uint64_t total_size) {
    uint64_t offset{};

    auto base = root / torrent_name;

    if (file_list.empty()) {
        _output_files.push_back({ base, total_size, 0 });
        return;
    }

    for (const auto& file: file_list) {
        _output_files.push_back({ base / file.path, file.length, offset });
        offset += file.length;
    }

    for (const auto& f: _output_files) {
        std::filesystem::create_directories(f.path.parent_path());

        if (!std::filesystem::exists(f.path)) {
            std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
            // preallocate space to prevent disk thrashing
            out.seekp(f.length - 1);
            out.write("", 1);   
        }
    }

    if (!std::filesystem::exists(savefile)) std::ofstream out(savefile, std::ios::binary | std::ios::trunc);
}

boost::asio::awaitable<void> FileManager::write_piece(uint32_t piece, std::vector<unsigned char> data) {

    uint64_t piece_offset = uint64_t(piece) * standard_piece_length;
    uint64_t remaining = data.size();
    uint64_t data_offset = 0;

    for (const auto& f : _output_files) {
        if (piece_offset >= f.offset + f.length) continue;
        if (piece_offset + remaining <= f.offset) break;

        uint64_t file_offset = piece_offset > f.offset ? piece_offset - f.offset : 0;

        uint64_t write_size = std::min(remaining, f.length - file_offset);

        std::ofstream out(f.path, std::ios::binary | std::ios::in | std::ios::out);
        
        out.seekp(file_offset);
        out.write(reinterpret_cast<const char*>(data.data() + data_offset), write_size);

        remaining -= write_size;
        data_offset += write_size;
        piece_offset += write_size;

        if (remaining == 0) break;
    }

    mark_complete(piece);
    co_return;
}

boost::asio::awaitable<std::optional<std::vector<unsigned char>>> FileManager::read_block(uint32_t piece, uint32_t begin, uint32_t length) {
    std::vector<unsigned char> buffer(length);

    uint64_t piece_offset = uint64_t(piece) * standard_piece_length + begin;
    uint64_t remaining = length;
    uint64_t data_offset = 0;

    for (const auto& f : _output_files) {
        if (piece_offset >= f.offset + f.length) continue;
        if (piece_offset + remaining <= f.offset) break;

        uint64_t file_offset = piece_offset > f.offset ? piece_offset - f.offset : 0;

        uint64_t read_size = std::min(remaining, f.length - file_offset);

        std::ifstream in(f.path, std::ios::binary);
        if (!in) co_return std::nullopt;

        in.seekg(file_offset);
        in.read(reinterpret_cast<char*>(buffer.data() + data_offset), read_size);

        if (!in) co_return std::nullopt;

        remaining   -= read_size;
        data_offset += read_size;
        piece_offset += read_size;

        if (remaining == 0) break;
    }

    co_return buffer;
}

std::optional<std::vector<uint32_t>> FileManager::read_save_file() {
    if (std::filesystem::exists(savefile)) {
        std::ifstream in(savefile, std::ios::binary);

        std::vector<uint32_t> out;

        uint32_t piece;
        while (in.read(reinterpret_cast<char*>(&piece), sizeof(piece))) out.push_back(piece);

        return out;
    }
    return std::nullopt;
}

void FileManager::mark_complete(uint32_t piece) {
    std::ofstream out(savefile, std::ios::app | std::ios::binary | std::ios::out);
    out.write(reinterpret_cast<const char*>(&piece), sizeof(piece));
}