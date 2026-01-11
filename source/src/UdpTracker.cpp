    #include "UdpTracker.hpp"

    uint32_t UdpTracker::random_u32() {
        static std::mt19937 rng{ std::random_device{}() };
        return rng();
    }

    boost::asio::awaitable<bool> UdpTracker::send_connect() {
        size_t off{};
        std::array<unsigned char, 16> buf{};
        boost::system::error_code ec;

        if (!connected) {
            co_await _socket.async_connect(_endpoint, net::redirect_error(net::use_awaitable, ec));
            if (ec) co_return false;
            connected = true;
        }

        auto write_64 = [&](uint64_t value) -> void {
            boost::endian::native_to_big_inplace(value);
            std::memcpy(buf.data() + off, &value, 8);
            off += 8;
        };

        auto write_32 = [&](uint32_t value) -> void {
            boost::endian::native_to_big_inplace(value);
            std::memcpy(buf.data() + off, &value, 4);
            off += 4;
        };

        auto transaction_id = random_u32();

        write_64(0x41727101980ULL);
        write_32(0);
        write_32(transaction_id);

        co_await _socket.async_send(net::buffer(buf), net::redirect_error(net::use_awaitable, ec));
        if (ec) co_return false;

        std::array<unsigned char, 16> response_buf{};
        co_await _socket.async_receive(net::buffer(response_buf), net::redirect_error(net::use_awaitable, ec));
        if (ec) co_return false;

        uint32_t action, return_transaction_id;
        uint64_t conn;

        std::memcpy(&action, response_buf.data(), 4);
        std::memcpy(&return_transaction_id, response_buf.data() + 4, 4);
        std::memcpy(&conn, response_buf.data() + 8, 8);

        boost::endian::big_to_native_inplace(action);
        boost::endian::big_to_native_inplace(return_transaction_id);
        boost::endian::big_to_native_inplace(conn);

        if (action != 0 || transaction_id != return_transaction_id) co_return false;
        _connection_id = conn;
        _conn_expiry = std::chrono::steady_clock::now() + std::chrono::seconds(60);

        co_return true;
    }

    boost::asio::awaitable<TrackerResponse> UdpTracker::send_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total) {
        size_t off{};
        std::array<unsigned char, 98> buf{};
        boost::system::error_code ec;

        auto write_64 = [&](uint64_t value) -> void {
            boost::endian::native_to_big_inplace(value);
            std::memcpy(buf.data() + off, &value, 8);
            off += 8;
        };

        auto write_32 = [&](uint32_t value) -> void {
            boost::endian::native_to_big_inplace(value);
            std::memcpy(buf.data() + off, &value, 4);
            off += 4;
        };

        auto write_16 = [&](uint16_t value) -> void {
            boost::endian::native_to_big_inplace(value);
            std::memcpy(buf.data() + off, &value, 2);
            off += 2;
        };

        auto transaction_id = random_u32();

        write_64(_connection_id);
        write_32(1); // announce
        write_32(transaction_id);

        std::memcpy(buf.data() + off, _info_hash.data(), 20); off += 20;
        std::memcpy(buf.data() + off, peer_id.data(), 20); off += 20;

        write_64(downloaded); // downloaded
        write_64(total - downloaded); // left
        write_64(uploaded); // uploaded

        uint32_t event = 0; // default: none

        if (downloaded == 0) {
            event = 2; // started
        }
        // later when completed:
        // event = 1;
        // on shutdown:
        // event = 3;

        write_32(event);
        write_32(0); // ip, 0 - default  
        write_32(random_u32()); // key
        write_32(0xFFFFFFFF); // want (-1 - all peers)
        write_16(6881); // port

        co_await _socket.async_send(net::buffer(buf), net::redirect_error(net::use_awaitable, ec));
        if (ec) co_return TrackerResponse{ {}, 180, ec.message() };

        std::array<unsigned char, 1500> response_buf{};

        auto size = co_await _socket.async_receive(net::buffer(response_buf), net::redirect_error(net::use_awaitable, ec));
        if (ec) co_return TrackerResponse{ {}, 180, ec.message() };

        if (size < 20) co_return TrackerResponse{ {}, 180, "Incomplete UDP packet"};

        TrackerResponse out;

        uint32_t action, return_transaction_id, interval;

        std::memcpy(&action, response_buf.data(), 4);
        std::memcpy(&return_transaction_id, response_buf.data() + 4, 4);
        std::memcpy(&interval, response_buf.data() + 8, 4);

        boost::endian::big_to_native_inplace(action);
        boost::endian::big_to_native_inplace(return_transaction_id);
        boost::endian::big_to_native_inplace(interval);

        if (action != 1 || return_transaction_id != transaction_id) co_return TrackerResponse{ {}, 180, "Invalid UDP announce response"};

        out.interval = interval;

        // parse ipv4 peers, 4 bytes -> address, 2 bytes -> port
        for (size_t i{20}; i + 6 <= size; i += 6) {
            auto* peer = response_buf.data() + i;

            boost::asio::ip::address_v4::bytes_type bytes {
                peer[0], peer[1], peer[2], peer[3]
            };
            auto ip = boost::asio::ip::make_address_v4(bytes);
            
            uint16_t port = (peer[4] << 8) | peer[5];

            out.peers.emplace_back(ip, port, "");
        }

        co_return out;
    }

    boost::asio::awaitable<bool> UdpTracker::ensure_socket_ready() {
        boost::system::error_code ec;
        
        if (!_socket.is_open()) {
            _socket.open(udp::v4(), ec);
            if (ec) co_return false;

            _socket.bind(udp::endpoint(udp::v4(), 0), ec);
            if (ec) co_return false;
        }

        if (_endpoint.port() == 0) {
            udp::resolver resolver(_exec);

            auto results = co_await resolver.async_resolve(udp::v4(), _host, std::to_string(_port), net::redirect_error(net::use_awaitable, ec));
            if (ec) co_return false;

            _endpoint = *results.begin();
        }

        co_return true;
    }

    boost::asio::awaitable<TrackerResponse> UdpTracker::async_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total) {
        try {

            if (!co_await ensure_socket_ready()) co_return TrackerResponse{ {}, 180, "Tracker unreachable" };

            auto now = std::chrono::steady_clock::now();

            if (_connection_id == 0 || now >= _conn_expiry) {
                if (!co_await send_connect()) co_return TrackerResponse{ {}, 180, "UDP connect failed"};
            }

            co_return co_await send_announce(peer_id, downloaded, uploaded, total);
        }

        catch (const std::exception& e) {
            co_return TrackerResponse{ {}, 180, e.what() };
        }
    } 
