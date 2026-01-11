#pragma once

#include <boost/dynamic_bitset.hpp>
#include <boost/asio.hpp>

#include <span>

class FileManager;

// strand owned

class PieceManager
{
public:
    PieceManager(boost::asio::any_io_executor exec, size_t num_pieces, size_t piece_length, size_t total_size, const std::vector<std::array<unsigned char, 20>>& piece_hashes, FileManager& fm, std::function<void(uint32_t)> callback): 
        _exec(exec),
        pm_strand(boost::asio::make_strand(_exec)),
        _num_pieces(num_pieces),
        _piece_length(piece_length),
        _total_size(total_size),
        _piece_hashes(piece_hashes),
        _fm(fm),
        _piece_complete_callback(std::move(callback))
    {
        _my_bitfield.resize((_num_pieces + 7) / 8);
        _pieces.resize(_num_pieces);
    }
    ~PieceManager() = default;

    // these dont modify state, dont need a strand
    uint64_t downloaded_bytes() const;
    uint64_t uploaded_bytes() const;
    uint64_t total_bytes() const;
    bool is_complete() const;
    bool is_piece_complete(uint32_t piece) const;
    size_t piece_length_for_index(int piece_index) const;

    // async public APIs
    [[nodiscard]] boost::asio::awaitable<std::vector<uint8_t>> async_fetch_my_bitset() const;
    [[nodiscard]] boost::asio::awaitable<std::optional<std::tuple<int, int, int>>> async_next_block_request(const boost::dynamic_bitset<>& peer_bitfield);
    [[nodiscard]] boost::asio::awaitable<void> async_add_block(uint32_t piece, uint32_t begin, std::span<const unsigned char> block);
    [[nodiscard]] boost::asio::awaitable<void> async_return_block(uint32_t piece, uint32_t begin);
    [[nodiscard]] boost::asio::awaitable<bool> async_should_be_interested(std::optional<uint32_t> piece = std::nullopt);
    [[nodiscard]] boost::asio::awaitable<std::optional<std::vector<unsigned char>>> async_fetch_block(uint32_t piece, uint32_t begin, uint32_t length);

private:
    bool should_be_interested(std::optional<uint32_t> piece = std::nullopt);
    std::optional<std::tuple<int, int, int>> next_block_request(const boost::dynamic_bitset<>& peer_bitfield);
    void add_block(uint32_t piece, uint32_t begin, std::span<const unsigned char> block);
    inline bool endgame_required() const;
    void set_my_bitfield(uint32_t piece);
    void return_block(uint32_t piece, uint32_t begin);
    std::vector<uint8_t> fetch_my_bitset() const;

    boost::asio::any_io_executor _exec;
    boost::asio::strand<boost::asio::any_io_executor> pm_strand;

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

    // it looks really bad, try to bridge the gap between an async context and a blocking thread
    template <typename T, typename StartFn>
    boost::asio::awaitable<T> await_callback(StartFn&& start) {
        auto token = boost::asio::use_awaitable;

        co_return co_await boost::asio::async_initiate<
            decltype(token),
            void(T)
        >(
            [&](auto handler) {
                start(std::move(handler));
            },
            token
        );
    }
};