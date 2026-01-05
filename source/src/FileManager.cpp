#include "FileManager.hpp"
#include "MetadataParser.hpp"

#include <fstream>

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

        std::ofstream out(f.path, std::ios::binary | std::ios::trunc);

        // preallocate space to prevent disk thrashing
        out.seekp(f.length - 1);
        out.write("", 1);   
    }
}

void FileManager::enqueue_piece(uint32_t piece, std::vector<unsigned char>&& data) {
    {
        std::scoped_lock lock(queue_mutex);
        queue.emplace(piece, std::move(data));
    }
    cv.notify_one();
}

void FileManager::worker_loop() {
    while (1) {
        WriteJob wj;

        {
            std::unique_lock lock(queue_mutex); 
            cv.wait(lock, [&] {
                return stop || !queue.empty();
            });

            if (stop && queue.empty()) break;
            
            wj = std::move(queue.front());
            queue.pop();
        }

        write_to_disk(wj.piece, std::move(wj.data));
    }
}

void FileManager::write_to_disk(uint32_t piece, std::vector<unsigned char>&& data) {
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
}