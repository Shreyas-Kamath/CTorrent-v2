#include "FileManager.hpp"
#include "MetadataParser.hpp"

#include <boost/asio.hpp>

// make a list of output files with offsets
void FileManager::build_output_files(std::filesystem::path root, std::string_view torrent_name, std::vector<TorrentFile>& file_list, uint64_t total_size) {
    uint64_t offset{};

    auto base = root / torrent_name;

    if (file_list.empty()) {
        OutputFile out = { std::fstream(base), total_size, 0 };
        output_files.push_back(std::move(out));
    }

    for (const auto& file: file_list) {
        auto path = base / file.path;

        std::filesystem::create_directories(path.parent_path());

        if (!std::filesystem::exists(path)) std::ofstream(path, std::ios::binary).close();

        std::filesystem::resize_file(path, file.length);

        OutputFile out;
        out.length = file.length;
        out.offset = offset;
        out.handle.open(path, std::ios::binary | std::ios::in | std::ios::out);

        output_files.emplace_back(std::move(out));

        offset += file.length;
    }

    auto savefile_path = root / (std::string(torrent_name) + ".fastresume");
    if (!std::filesystem::exists(savefile_path)) std::ofstream(savefile_path, std::ios::binary).close();
    savefile.open(savefile_path, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
}

boost::asio::awaitable<void> FileManager::write_piece(uint32_t piece, std::vector<unsigned char> data) {

    uint64_t piece_offset = uint64_t(piece) * standard_piece_length;
    uint64_t remaining = data.size();
    uint64_t data_offset = 0;

    auto start = std::ranges::upper_bound(output_files, piece_offset, {}, &OutputFile::offset);
    if (start != output_files.begin()) start = prev(start);

    while (remaining > 0) {
        uint64_t file_offset = piece_offset > start->offset ? piece_offset - start->offset : 0;
        uint64_t write_size = std::min(remaining, start->length - file_offset);
        
        start->handle.seekp(file_offset);
        start->handle.write(reinterpret_cast<const char*>(data.data() + data_offset), write_size);

        remaining -= write_size;
        data_offset += write_size;
        piece_offset += write_size;

        if (remaining == 0) break;

        start = next(start);
        assert(start != output_files.end() && "https://en.cppreference.com/cpp/algorithm/ranges/upper_bound");
    }

    mark_complete(piece);
    co_return;
}

boost::asio::awaitable<std::optional<std::vector<unsigned char>>> FileManager::read_block(uint32_t piece, uint32_t begin, uint32_t length) {
    std::vector<unsigned char> buffer(length);

    uint64_t piece_offset = uint64_t(piece) * standard_piece_length + begin;
    uint64_t remaining = length;
    uint64_t data_offset = 0;

    auto start = std::ranges::upper_bound(output_files, piece_offset, {}, &OutputFile::offset);
    if (start != output_files.begin()) start = prev(start);

    while (remaining > 0) {

        uint64_t file_offset = piece_offset > start->offset ? piece_offset - start->offset : 0;
        uint64_t read_size = std::min(remaining, start->length - file_offset);

        start->handle.seekg(file_offset);
        start->handle.read(reinterpret_cast<char*>(buffer.data() + data_offset), read_size);

        remaining   -= read_size;
        data_offset += read_size;
        piece_offset += read_size;

        if (remaining == 0) break;

        start = next(start);
        assert(start != output_files.end() && "https://en.cppreference.com/cpp/algorithm/ranges/upper_bound");
    }

    co_return buffer;
}

std::vector<uint32_t> FileManager::read_save_file() {
    // use a bitset for less space
    std::vector<uint32_t> out;
    uint32_t piece;

    savefile.clear();
    savefile.seekg(0, std::ios::beg);
    while (savefile.read(reinterpret_cast<char*>(&piece), sizeof(piece))) out.push_back(piece);

    savefile.clear();
    return out;
}

void FileManager::mark_complete(uint32_t piece) {
    savefile.write(reinterpret_cast<const char*>(&piece), sizeof(piece));
}