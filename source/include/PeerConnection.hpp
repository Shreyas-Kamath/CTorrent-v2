#pragma once

#include "Peer.hpp"
#include "Utils.hpp"

#include <memory>
#include <vector>
#include <chrono>
#include <span>

#include <boost/endian.hpp>
#include <boost/dynamic_bitset.hpp>

class PieceManager;

class PeerConnection: public std::enable_shared_from_this<PeerConnection> {

public:

    // outbound
    PeerConnection(boost::asio::any_io_executor exec, 
        const Peer& peer, 
        const std::array<unsigned char, 20>& info_hash, 
        const std::string& peer_id,
        size_t num_pieces,
        PieceManager& pm,
        PeerDirection dir): _exec(exec), _socket(_exec), p(peer), _info_hash(info_hash), _peer_id(peer_id), _num_pieces(num_pieces), _pm(pm), block_timeout_timer(_exec), socket_strand(boost::asio::make_strand(_exec)), direction(dir) 
        {
            _peer_bitfield.resize(_num_pieces, false);    
        }

    // inbound 
    PeerConnection(boost::asio::ip::tcp::socket&& socket,
        const Peer& peer,
        const std::array<unsigned char, 20>& info_hash,
        const std::string& peer_id,
        size_t num_pieces,
        PieceManager& pm, 
        PeerDirection dir): _exec(socket.get_executor()), _socket(std::move(socket)), _info_hash(info_hash), _peer_id(peer_id), _num_pieces(num_pieces), _pm(pm), block_timeout_timer(_exec), socket_strand(boost::asio::make_strand(_exec)), p(peer), direction(dir) 
        {
            _peer_bitfield.resize(_num_pieces, false);
        }
    
    Peer& peer() { return p; }

    [[nodiscard]] boost::asio::awaitable<void> start();
    void stop();
    [[nodiscard]] boost::asio::awaitable<void> send_have(uint32_t piece);

    double progress() const { return static_cast<double>(completed_pieces) * 100.0 / _num_pieces; }
    int requests() const { return _in_flight; }
    bool choked() const { return am_choked; }
    bool interested() const { return am_interested; }
    bool is_stopped() const { return stopped.load(std::memory_order_acquire); }

private:

    enum class Message_ID: uint8_t {
        Choke = 0, Unchoke, Interested,
        NotInterested, Have, Bitfield,
        Request, Piece, Cancel, Port
    };

    struct ParsedRequest {
        uint32_t piece, begin, length;
    };

    std::optional<ParsedRequest> parse_request() const;
    bool is_valid_upload_request(const ParsedRequest& r) const;

    struct InFlight {
        uint32_t piece, begin, length;
        std::chrono::steady_clock::time_point sent_at;
    };

    void build_handshake();
    bool validate_handshake();
    std::string decode_peer_id(std::string_view pid);
    
    [[nodiscard]] boost::asio::awaitable<void> handshake();
    [[nodiscard]] boost::asio::awaitable<void> message_loop();
    [[nodiscard]] boost::asio::awaitable<void> watchdog();

    boost::asio::any_io_executor _exec;
    boost::asio::ip::tcp::socket _socket;
    
    // helpers
    boost::asio::strand<boost::asio::any_io_executor> socket_strand;

    [[nodiscard]] boost::asio::awaitable<std::optional<uint32_t>> read_u32_be();
    [[nodiscard]] boost::asio::awaitable<std::optional<uint8_t>> read_u8();
    [[nodiscard]] boost::asio::awaitable<void> send_bitfield();
    [[nodiscard]] boost::asio::awaitable<void> send_interested();
    [[nodiscard]] boost::asio::awaitable<void> send_request(int piece_index, int begin, int length);
    [[nodiscard]] boost::asio::awaitable<void> send_cancel(uint32_t piece_index, uint32_t begin, uint32_t length);
    [[nodiscard]] boost::asio::awaitable<void> send_unchoke();

    void handle_message(Message_ID id);
    void handle_bitfield();
    void handle_have();
    [[nodiscard]] boost::asio::awaitable<void> maybe_request_next();
    void handle_piece();
    [[nodiscard]] boost::asio::awaitable<void> handle_request();

    // buffers
    std::vector<unsigned char> msg_buf;
    std::array<unsigned char, 5> _interested_buf;
    std::array<unsigned char, 68> _handshake_buf;
    
    boost::dynamic_bitset<> _peer_bitfield;

    Peer p;
    const std::array<unsigned char, 20> _info_hash;
    std::string _peer_id;
    size_t _num_pieces;
    size_t completed_pieces{};

    // state
    int _in_flight = 0;
    int MAX_IN_FLIGHT = 16;
    static constexpr auto REQUEST_TIMEOUT = std::chrono::seconds(10);
    boost::asio::steady_timer block_timeout_timer;
    std::vector<InFlight> in_flight_blocks;
    PeerDirection direction;

    std::chrono::steady_clock::time_point last_unchoked;
    std::chrono::steady_clock::time_point last_received;
    
    bool am_interested = false;
    bool am_choked = true;

    bool peer_choked = true;
    bool peer_interested = false;

    PieceManager& _pm;

    std::atomic<bool> stopped = false;
};