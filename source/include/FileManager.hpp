#pragma once

#include <filesystem>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <functional>

#include <boost/asio.hpp>

struct TorrentFile;

class FileManager {
public:
    using ReadCallback = std::move_only_function<void(std::optional<std::vector<unsigned char>>)>;

    FileManager(std::filesystem::path root, std::string_view torrent_name, std::vector<TorrentFile>& file_list, uint64_t total_size, uint64_t piece_length, boost::asio::any_io_executor exec): standard_piece_length(piece_length), net_exec(exec) {
        savefile = std::string(torrent_name) + ".fastresume";            
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
    void enqueue_read_block(uint32_t piece, uint32_t begin, uint32_t length, ReadCallback cb);
    std::optional<std::vector<uint32_t>> read_save_file();

private:
    boost::asio::any_io_executor net_exec;

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
    struct ReadJob {
        uint32_t piece, begin, length;
        ReadCallback callback;
    };

    uint64_t standard_piece_length;

    std::thread worker;
    
    std::queue<WriteJob> write_queue;
    std::queue<ReadJob> read_queue;

    std::mutex queue_mutex;
    std::condition_variable cv;
    std::atomic<bool> stop{ false };

    void worker_loop();
    void mark_complete(uint32_t piece);
    void write_to_disk(uint32_t piece, std::vector<unsigned char>&& data);
    std::optional<std::vector<unsigned char>> read_from_disk(uint32_t piece, uint32_t begin, uint32_t length);
    
    std::string savefile;
};