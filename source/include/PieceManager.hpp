#pragma once

#include <boost/dynamic_bitset.hpp>
#include <boost/asio.hpp>

#include <span>
#include <print>

class FileManager;

class PieceManager
{
public:
    PieceManager(boost::asio::any_io_executor disk_exec, size_t num_pieces, size_t piece_length, size_t total_size, const std::vector<std::array<unsigned char, 20>>& piece_hashes, FileManager& fm, std::function<void(uint32_t)> callback);
    ~PieceManager() {
        std::println("Pm destroyed");
    }

    uint64_t downloaded_bytes() const;
    uint64_t uploaded_bytes() const;
    uint64_t total_bytes() const;
    bool is_complete() const;
    bool is_piece_complete(uint32_t piece) const;
    size_t piece_length_for_index(int piece_index) const;

    // public APIs
    [[nodiscard]] std::vector<uint8_t> fetch_my_bitset() const;
    [[nodiscard]] std::optional<std::tuple<int, int, int>> next_block_request(const boost::dynamic_bitset<>& peer_bitfield);
    [[nodiscard]] void add_block(uint32_t piece, uint32_t begin, std::span<const unsigned char> block);
    [[nodiscard]] void return_block(uint32_t piece, uint32_t begin);
    [[nodiscard]] boost::asio::awaitable<std::optional<std::vector<unsigned char>>> async_fetch_block(uint32_t piece, uint32_t begin, uint32_t length);

private:
    bool endgame_required() const;
    void set_my_bitfield(uint32_t piece);

    boost::asio::any_io_executor _disk_exec;

    void lazy_init(uint32_t piece_index);
    bool verify_hash(uint32_t piece_index);

    enum class BlockState {
        NotRequested = 0,
        Requested,
        Received
    };

    struct PieceBuffer {
        std::vector<unsigned char> data;
        std::vector<BlockState> block_status;
        int blocks_received{};
        bool is_complete = false;
    };

    std::vector<PieceBuffer> _pieces;

    std::vector<uint8_t> _my_bitfield;

    size_t _num_pieces;
    size_t _piece_length;
    size_t _total_size;
    size_t _completed_pieces{};
    const std::vector<std::array<unsigned char, 20>>& _piece_hashes;

    uint64_t downloaded{}, uploaded{};

    bool endgame = false;
    uint32_t endgame_cursor = 0;

    std::function<void(uint32_t)> _piece_complete_callback;

    FileManager& _fm;
};