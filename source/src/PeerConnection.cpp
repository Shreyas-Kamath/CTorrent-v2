#include "PeerConnection.hpp"
#include "PieceManager.hpp"

#include <iostream>
#include <span>
#include <print>

boost::asio::awaitable<void> PeerConnection::start() {

    if (direction == PeerDirection::Outbound) {
        boost::asio::ip::tcp::endpoint endpoint(p.addr(), p.port());
        boost::system::error_code ec;

        co_await _socket.async_connect(endpoint, boost::asio::redirect_error(boost::asio::use_awaitable, ec));

        // most likely a dead / saturated / firewalled peer
        if (ec) { 
            co_await stop();
            co_return;
        }

        co_await handshake();
    }

    else if (direction == PeerDirection::Inbound) {
        build_handshake();

        boost::system::error_code ec;
        co_await boost::asio::async_write(_socket, boost::asio::buffer(_handshake_buf), boost::asio::bind_executor(write_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));
            
        // bad connection
        if (ec) {
            co_await stop();
            co_return;
        }
    }

    last_received = std::chrono::steady_clock::now();

    co_await send_bitfield();

    auto self = shared_from_this();
    co_spawn(_exec,
        [self]() -> boost::asio::awaitable<void> {
            co_await self->watchdog();
        },
        boost::asio::detached
    );
    co_await message_loop();
}

// kill connection and signal to client that we wish to remove it
// we also clean up all its blocks, if any
boost::asio::awaitable<void> PeerConnection::stop() {
    if (stopped.exchange(true, std::memory_order_release)) co_return;

    boost::system::error_code ec;

    block_timeout_timer.cancel();
    _socket.cancel(ec);
    _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    _socket.close(ec);

    co_return;
}

// protocol
void PeerConnection::build_handshake() {
    _handshake_buf[0] = 19;

    std::memcpy(&_handshake_buf[1], "BitTorrent protocol", 19);
    std::memset(&_handshake_buf[20], 0, 8);
    std::memcpy(&_handshake_buf[28], _info_hash.data(), 20);
    std::memcpy(&_handshake_buf[48], _peer_id.data(), 20);
}

// build handshake, send, then verify response
boost::asio::awaitable<void> PeerConnection::handshake() {
    build_handshake();

    boost::system::error_code ec;
    co_await boost::asio::async_write(_socket, boost::asio::buffer(_handshake_buf), boost::asio::bind_executor(write_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));
        
    // bad connection
    if (ec) {
        co_await stop();
        co_return;
    }

    co_await boost::asio::async_read(_socket, boost::asio::buffer(_handshake_buf), boost::asio::bind_executor(read_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));

    // no errors allowed during handshake
    if (ec) {
        co_await stop();
        co_return;
    }

    // bad peer or poor network
    if (!validate_handshake()) {
        // std::cout << "Handshake failed with peer " << p.ip();
        co_await stop();
        co_return;
    }
}

boost::asio::awaitable<void> PeerConnection::send_bitfield() {
    auto my_bitfield = co_await _pm.async_fetch_my_bitset();

    uint32_t len = boost::endian::native_to_big(1 + static_cast<uint32_t>(my_bitfield.size()));

    std::vector<uint8_t> msg; msg.reserve(4 + 1 + my_bitfield.size());

    msg.insert(msg.end(), reinterpret_cast<uint8_t*>(&len), reinterpret_cast<uint8_t*>(&len) + 4);
    msg.push_back(static_cast<uint8_t>(Message_ID::Bitfield));
    msg.insert(msg.end(), my_bitfield.begin(), my_bitfield.end());

    boost::system::error_code ec;
    co_await boost::asio::async_write(_socket, boost::asio::buffer(msg), boost::asio::bind_executor(write_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));

    if (ec) {
        co_await stop();
        co_return;
    }
}

// check if the incoming handshake is valid
bool PeerConnection::validate_handshake() {

    const unsigned char* incoming_info_hash = _handshake_buf.data() + 28;
    if (std::equal(_info_hash.begin(), _info_hash.end(), incoming_info_hash, incoming_info_hash + 20)) {
        const unsigned char* peer_id = _handshake_buf.data() + 48;

        p.id() = decode_peer_id(std::string_view(reinterpret_cast<const char*>(peer_id), 20));
        return true;
    }
    return false;
}

std::string PeerConnection::decode_peer_id(std::string_view pid) {
    if (pid.size() != 20)
        return "Unknown";

    // Azureus-style: -XXYYYY-
    if (pid[0] == '-' && pid[7] == '-') {
        std::string_view code = pid.substr(1, 2);
        std::string_view ver  = pid.substr(3, 4);

        auto format_ver = [](std::string_view v) {
            return std::format("{}.{}.{}", v[0], v[1], v[2]);
        };

        if (code == "qB") return "qBittorrent "  + format_ver(ver);
        if (code == "TR") return "Transmission " + format_ver(ver);
        if (code == "UT") return "µTorrent "     + format_ver(ver);
        if (code == "LT") return "libtorrent "   + format_ver(ver);
        if (code == "AZ") return "Azureus "      + format_ver(ver);
    }
    return "Unknown";
}

// helpers
boost::asio::awaitable<std::optional<uint32_t>> PeerConnection::read_u32_be() {
    std::array<char, 4> length_buf{};

    boost::system::error_code ec;
    co_await boost::asio::async_read(_socket, boost::asio::buffer(length_buf), boost::asio::bind_executor(read_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));

    if (ec) {
        co_await stop();
        co_return std::nullopt;
    }

    uint32_t len;
    std::memcpy(&len, length_buf.data(), 4);
    boost::endian::big_to_native_inplace(len);
    co_return len;
}

boost::asio::awaitable<std::optional<uint8_t>> PeerConnection::read_u8() {
    uint8_t id{};

    boost::system::error_code ec;
    co_await boost::asio::async_read(_socket, boost::asio::buffer(&id, 1), boost::asio::bind_executor(read_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));

    if (ec) {
        co_await stop();
        co_return std::nullopt;
    }

    co_return id;
}

// modify bitfield when peer sends theirs
void PeerConnection::handle_bitfield(std::span<const unsigned char> msg_buf) {
    size_t bit_index{};

    for (auto byte: msg_buf) {
        for (int i{7}; i >= 0; --i) {
            if (bit_index >= _peer_bitfield.size()) return;

            bool has_piece = (byte >> i) & 1;
            _peer_bitfield.set(bit_index, has_piece);
            if (has_piece) ++completed_pieces;
            ++bit_index;
        }
    }
}

// modify bitfield when peer sends a HAVE
void PeerConnection::handle_have(std::span<const unsigned char> msg_buf) {
    uint32_t index;
    std::memcpy(&index, msg_buf.data(), 4);
    boost::endian::big_to_native_inplace(index);

    if (index < _peer_bitfield.size() && !_peer_bitfield.test(index)) {
        _peer_bitfield.set(index);
        ++completed_pieces;
    }
}

boost::asio::awaitable<void> PeerConnection::maybe_request_next() {
    while (!am_choked && _in_flight < MAX_IN_FLIGHT) {
        auto req = co_await _pm.async_next_block_request(_peer_bitfield);
        if (!req) break;

        auto [piece, offset, length] = req.value();
        co_await send_request(piece, offset, length);
    }
    co_return;
}

// handle incoming piece
boost::asio::awaitable<void> PeerConnection::handle_piece(std::span<const unsigned char> msg_buf) {
    if (msg_buf.size() < 8) co_return;

    uint32_t piece, begin;
    std::memcpy(&piece, msg_buf.data(), 4);
    std::memcpy(&begin, msg_buf.data() + 4, 4);

    boost::endian::big_to_native_inplace(piece);
    boost::endian::big_to_native_inplace(begin);

    auto pos = std::ranges::find_if(in_flight_blocks,
        [piece, begin](const InFlight& inflight) {
            return inflight.piece == piece && inflight.begin == begin;
        }
    );

    if (pos != in_flight_blocks.end()) {
        *pos = in_flight_blocks.back();
        in_flight_blocks.pop_back();
        --_in_flight;
    }

    auto block = std::span<const unsigned char>(msg_buf).subspan(8);
    co_await _pm.async_add_block(piece, begin, block);
}

// indicate interest to the peer
boost::asio::awaitable<void> PeerConnection::send_interested() {
    uint32_t len{1};
    boost::endian::native_to_big_inplace(len);
    std::memcpy(_interested_buf.data(), &len, 4);
    _interested_buf[4] = static_cast<unsigned char>(Message_ID::Interested);

    boost::system::error_code ec;
    co_await boost::asio::async_write(_socket, boost::asio::buffer(_interested_buf), boost::asio::bind_executor(write_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));

    if (ec) {
        co_await stop();
        co_return;
    }
}

// ask for a piece
boost::asio::awaitable<void> PeerConnection::send_request(int piece_index, int begin, int length) {
    std::array<char, 17> request_buf{};

    uint32_t len = 13;
    boost::endian::native_to_big_inplace(len);
    std::memcpy(request_buf.data(), &len, 4);

    request_buf[4] = static_cast<unsigned char>(Message_ID::Request);

    uint32_t be_piece_index = boost::endian::native_to_big<uint32_t>(piece_index);
    std::memcpy(request_buf.data() + 5, &be_piece_index, 4);

    uint32_t be_begin = boost::endian::native_to_big<uint32_t>(begin);
    std::memcpy(request_buf.data() + 9, &be_begin, 4);
    
    uint32_t be_length = boost::endian::native_to_big<uint32_t>(length);   
    std::memcpy(request_buf.data() + 13, &be_length, 4);

    ++_in_flight;
    in_flight_blocks.emplace_back(piece_index, begin, length, std::chrono::steady_clock::now());

    boost::system::error_code ec;
    co_await boost::asio::async_write(_socket, boost::asio::buffer(request_buf), boost::asio::bind_executor(write_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));

    if (ec) {
        co_await stop();
        co_return;
    }
}

boost::asio::awaitable<void> PeerConnection::send_cancel(uint32_t piece_index, uint32_t begin, uint32_t length) {
    std::array<char, 17> cancel_buf{};

    uint32_t msg_len = 13;
    boost::endian::native_to_big_inplace(msg_len);

    std::memcpy(cancel_buf.data(), &msg_len, 4);

    cancel_buf[4] = static_cast<unsigned char>(Message_ID::Cancel);

    auto be_piece = boost::endian::native_to_big(piece_index);
    auto be_begin = boost::endian::native_to_big(begin);
    auto be_length = boost::endian::native_to_big(length);

    std::memcpy(cancel_buf.data() + 5, &be_piece, 4);
    std::memcpy(cancel_buf.data() + 9, &be_begin, 4);
    std::memcpy(cancel_buf.data() + 13, &be_length, 4);

    boost::system::error_code ec;
    co_await boost::asio::async_write(_socket, boost::asio::buffer(cancel_buf), boost::asio::bind_executor(write_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));

    if (ec) {
        co_await stop();
        co_return;
    }
}

boost::asio::awaitable<void> PeerConnection::send_unchoke() {
    std::array<char, 5> buf{};

    uint32_t len = boost::endian::native_to_big<uint32_t>(1);
    std::memcpy(buf.data(), &len, 4);

    buf[4] = static_cast<char>(Message_ID::Unchoke);    

    boost::system::error_code ec;
    co_await boost::asio::async_write(_socket, boost::asio::buffer(buf), boost::asio::bind_executor(write_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));

    if (ec) {
        co_await stop();
        co_return;
    }

    // std::println("{} was unchoked", p.addr().to_string());
    peer_choked = false;
}

boost::asio::awaitable<void> PeerConnection::send_have(uint32_t piece) {
    uint32_t len = boost::endian::native_to_big<uint32_t>(5);
    std::array<char, 9> buf{};

    std::memcpy(buf.data(), &len, 4);
    buf[4] = static_cast<char>(Message_ID::Have);

    uint32_t be_piece = boost::endian::native_to_big<uint32_t>(piece);

    std::memcpy(buf.data() + 5, &be_piece, 4);

    boost::system::error_code ec;

    co_await boost::asio::async_write(_socket, boost::asio::buffer(buf), boost::asio::bind_executor(write_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));
    if (ec) {
        co_await stop();
        co_return;
    }
}

void PeerConnection::handle_message(Message_ID id) {
    switch (id)
    {
    case Message_ID::NotInterested:
        peer_interested = false;
        break;

    case Message_ID::Cancel:
        // std::cout << "Peer sent cancel\n";
        break;
    case Message_ID::Port:
        // std::cout << "Peer sent port\n";
        break;
    default:
        break;
    }
}

// end helpers

boost::asio::awaitable<void> PeerConnection::message_loop() {
        while (!stopped.load(std::memory_order_acquire)) {
            auto len = co_await read_u32_be();

            last_received = std::chrono::steady_clock::now();
            
            if (!len) co_return;
            if (len.value() == 0) continue;

            auto msg_id = co_await read_u8();
            if (!msg_id) co_return;
            auto id = static_cast<Message_ID>(msg_id.value());

            std::vector<unsigned char> msg_buf((len.value() - 1));

            boost::system::error_code ec;
            co_await boost::asio::async_read(_socket, boost::asio::buffer(msg_buf), boost::asio::bind_executor(read_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));
            
            if (ec) {
                co_await stop();
                co_return;
            } 

            std::span<const unsigned char> span{msg_buf};

            switch (id) {
                case Message_ID::Request:
                    if (!peer_choked) co_await handle_request(span);
                    break;

                case Message_ID::Choke:
                    am_choked = true;
                    break;

                case Message_ID::Unchoke:
                    am_choked = false;
                    last_unchoked = std::chrono::steady_clock::now();
                    if (am_interested) co_await maybe_request_next();
                    break;

                case Message_ID::Interested:
                    peer_interested = true;
                    // std::println("{} sent interested", p.addr().to_string());
                    if (peer_choked) co_await send_unchoke();
                    break;

                case Message_ID::Piece:
                    co_await handle_piece(span);
                    co_await maybe_request_next();
                    break;

                case Message_ID::Have:
                    handle_have(span);
                    if (!am_interested) {
                        co_await send_interested();
                        am_interested = true;
                    }
                    break;

                case Message_ID::Bitfield:
                    handle_bitfield(span);
                    if (!am_interested) {
                        co_await send_interested();
                        am_interested = true;
                    }
                    break;      

                default:
                    handle_message(id);
                    break;
            }
        }
}

boost::asio::awaitable<void> PeerConnection::watchdog() {
    boost::system::error_code ec;
    while (!stopped.load(std::memory_order_acquire)) {

        block_timeout_timer.expires_after(std::chrono::seconds(1));
        co_await block_timeout_timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));

        if (stopped.load(std::memory_order_acquire) || ec) {
            ec.clear();
            break;
        }

        auto now = std::chrono::steady_clock::now();

        if (am_choked) {
            if (now - last_received > std::chrono::minutes(2) && now - last_unchoked > std::chrono::minutes(2)) {
                co_await stop();
                co_return;
            }
        }

        for (size_t i = 0; i < in_flight_blocks.size();) {
            auto& curr = in_flight_blocks[i];

            if (now - curr.sent_at >= REQUEST_TIMEOUT) {
                co_await _pm.async_return_block(curr.piece, curr.begin);

                in_flight_blocks[i] = in_flight_blocks.back();
                in_flight_blocks.pop_back();

                if (_in_flight > 0) --_in_flight;
            } else ++i;
        }

        // == 0?
        if (!am_choked && am_interested && _in_flight < MAX_IN_FLIGHT) co_await maybe_request_next();
    }

    // last pass to clear blocks after stopped
    for (auto& block: in_flight_blocks) co_await _pm.async_return_block(block.piece, block.begin);

    in_flight_blocks.clear();
    _in_flight = 0;

    co_return;
}

std::optional<PeerConnection::ParsedRequest> PeerConnection::parse_request(std::span<const unsigned char> msg_buf) const {
    if (msg_buf.size() != 12) return std::nullopt;

    ParsedRequest req;

    std::memcpy(&req.piece, msg_buf.data(), 4);
    std::memcpy(&req.begin, msg_buf.data() + 4, 4);
    std::memcpy(&req.length, msg_buf.data() + 8, 4);

    boost::endian::big_to_native_inplace(req.piece);
    boost::endian::big_to_native_inplace(req.begin);
    boost::endian::big_to_native_inplace(req.length);

    return req;
}

bool PeerConnection::is_valid_upload_request(const ParsedRequest& r) const {
    if (peer_choked) return false;
    if (r.piece >= _num_pieces) return false;
    if (r.length == 0 || r.length > 16 * 1024) return false;
    if (r.begin % 16384 != 0) return false;

    auto piece_size = _pm.piece_length_for_index(r.piece);

    if (r.begin + r.length > piece_size) return false;
    if (!_pm.is_piece_complete(r.piece)) return false;

    return true;
}

boost::asio::awaitable<void> PeerConnection::handle_request(std::span<const unsigned char> msg_buf) {
    // std::println("{} is requesting a block", p.addr().to_string());
    auto parsed = parse_request(msg_buf);
    if (!parsed) co_return;
    if (!is_valid_upload_request(parsed.value())) co_return;
     
    auto [piece, begin, length] = *parsed;
    auto block = co_await _pm.async_fetch_block(piece, begin, length);

    if (!block || stopped) co_return;

    uint32_t msg_len = 9 + static_cast<uint32_t>(block->size());
    uint32_t be_len = boost::endian::native_to_big(msg_len);
    uint32_t be_piece = boost::endian::native_to_big(piece);
    uint32_t be_begin = boost::endian::native_to_big(begin);

    std::vector<unsigned char> buf{}; buf.reserve(4 + msg_len);

    buf.insert(buf.end(), reinterpret_cast<unsigned char*>(&be_len), reinterpret_cast<unsigned char*>(&be_len) + 4);
    buf.push_back(static_cast<unsigned char>(Message_ID::Piece));
    buf.insert(buf.end(), reinterpret_cast<unsigned char*>(&be_piece), reinterpret_cast<unsigned char*>(&be_piece) + 4);
    buf.insert(buf.end(), reinterpret_cast<unsigned char*>(&be_begin), reinterpret_cast<unsigned char*>(&be_begin) + 4);

    buf.insert(buf.end(), block->begin(), block->end());

    boost::system::error_code ec;
    co_await boost::asio::async_write(_socket, boost::asio::buffer(buf), boost::asio::bind_executor(write_strand, boost::asio::redirect_error(boost::asio::use_awaitable, ec)));
    if (ec) {
        co_await stop();
        co_return;
    }
}