#pragma once

#include <filesystem>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>

struct TorrentFile;

class FileManager {
public:
    FileManager(std::filesystem::path root, std::string_view torrent_name, std::vector<TorrentFile>& file_list, uint64_t total_size, uint64_t piece_length): standard_piece_length(piece_length) {
        build_output_files(root, torrent_name, file_list, total_size);

        worker = std::thread(&FileManager::worker_loop, this);
    }

    ~FileManager() noexcept {
        {
            std::scoped_lock lock(queue_mutex);
            stop = true;
        }
        cv.notify_all();
        worker.join();
    }

    void enqueue_piece(uint32_t piece, std::vector<unsigned char>&& data);

private:
    struct OutputFile {
        std::filesystem::path path;
        uint64_t length, offset;
    };

    std::vector<OutputFile> _output_files;

    void build_output_files(std::filesystem::path root, std::string_view torrent_name, std::vector<TorrentFile>& file_list, uint64_t total_size);

    struct WriteJob {
        uint32_t piece;
        std::vector<unsigned char> data;
    };
    uint64_t standard_piece_length;

    std::thread worker;
    std::queue<WriteJob> queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::atomic<bool> stop{ false };

    void worker_loop();
    void write_to_disk(uint32_t piece, std::vector<unsigned char>&& data);
};