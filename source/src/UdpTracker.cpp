    #include "UdpTracker.hpp"

    uint32_t UdpTracker::random_u32() {
        static std::mt19937 rng{ std::random_device{}() };
        return rng();
    }

    boost::asio::awaitable<bool> UdpTracker::send_connect(UdpContext& context) {
        size_t off{};
        std::array<unsigned char, 16> buf{};
        boost::system::error_code ec;

        if (!context.connected) {
            co_await context.socket.async_connect(context.endpoint, net::redirect_error(net::use_awaitable, ec));
            if (ec) co_return false;
            context.connected = true;
        }
        auto transaction_id = random_u32();

        write_buffer(buf, 0x41727101980ULL, off);
        write_buffer(buf, static_cast<uint32_t>(0), off);
        write_buffer(buf, transaction_id, off);

        co_await context.socket.async_send(net::buffer(buf), net::redirect_error(net::use_awaitable, ec));
        if (ec) co_return false;

        std::array<unsigned char, 16> response_buf{};
        co_await context.socket.async_receive(net::buffer(response_buf), net::redirect_error(net::use_awaitable, ec));
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
        context.connection_id = conn;
        context.expiry = std::chrono::steady_clock::now() + std::chrono::seconds(60);

        co_return true;
    }

    boost::asio::awaitable<TrackerResponse> UdpTracker::send_announce(UdpContext& context, const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total) {
        size_t off{};
        std::array<unsigned char, 98> buf{};
        boost::system::error_code ec;

        auto transaction_id = random_u32();

        write_buffer(buf, context.connection_id, off);
        write_buffer(buf, static_cast<uint32_t>(1), off);
        write_buffer(buf, transaction_id, off);

        std::memcpy(buf.data() + off, _info_hash.data(), 20); off += 20;
        std::memcpy(buf.data() + off, peer_id.data(), 20); off += 20;

        write_buffer(buf, downloaded, off);
        write_buffer(buf, total - downloaded, off);
        write_buffer(buf, uploaded, off);

        uint32_t event = 0; // default: none

        if (downloaded == 0) event = 2; // started
        // later when completed:
        else if (downloaded == total) event = 1;
        // on shutdown:
        // event = 3;

        write_buffer(buf, event, off);
        write_buffer(buf, static_cast<uint32_t>(0), off);
        write_buffer(buf, random_u32(), off);
        write_buffer(buf, 0xFFFFFFFF, off);
        write_buffer(buf, static_cast<uint16_t>(6881), off);

        co_await context.socket.async_send(net::buffer(buf), net::redirect_error(net::use_awaitable, ec));
        if (ec) co_return TrackerResponse{ {}, 180, ec.message() };

        std::array<unsigned char, 1500> response_buf{};

        auto size = co_await context.socket.async_receive(net::buffer(response_buf), net::redirect_error(net::use_awaitable, ec));

        if (ec) co_return TrackerResponse{ {}, 180, ec.message() };

        if (size < 20) co_return TrackerResponse{ {}, 180, "Incomplete UDP packet"};

        uint32_t action, return_transaction_id, interval;

        std::memcpy(&action, response_buf.data(), 4);
        std::memcpy(&return_transaction_id, response_buf.data() + 4, 4);
        std::memcpy(&interval, response_buf.data() + 8, 4);

        boost::endian::big_to_native_inplace(action);
        boost::endian::big_to_native_inplace(return_transaction_id);
        boost::endian::big_to_native_inplace(interval);

        if (action != 1 || return_transaction_id != transaction_id) co_return TrackerResponse{ {}, 180, "Invalid UDP announce response"};

        TrackerResponse out;

        if (context.proto == udp::v4()) parse_v4(out, response_buf, size);
        else if (context.proto == udp::v6()) parse_v6(out, response_buf, size);

        out.interval = interval;

        co_return out;
    }

    boost::asio::awaitable<bool> UdpTracker::ensure_socket_ready(UdpContext& context) {
        boost::system::error_code ec;

        if (!context.bound) {
            context.socket.bind(udp::endpoint(context.proto, 0), ec);
            if (ec) co_return false;
            context.bound = true;
        }

        if (context.endpoint.port() == 0) {
            udp::resolver resolver(_exec);

            auto results = co_await resolver.async_resolve(context.proto, _host, std::to_string(_port), net::redirect_error(net::use_awaitable, ec));
            if (ec || results.empty()) co_return false;

            for (auto& r: results) {
                auto ep = r.endpoint();

                if (context.proto == udp::v4() && ep.address().is_v4()) context.endpoint = ep;
                else if (context.proto == udp::v6() && ep.address().is_v6()) context.endpoint = ep;
            }
        }

        co_return true;
    }

boost::asio::awaitable<TrackerResponse> UdpTracker::async_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total) {
    TrackerResponse out;
    bool any_success = false;

    auto now = std::chrono::steady_clock::now();

    // IPv4 path
    if (udp_v4) {
        try {
            if (co_await ensure_socket_ready(*udp_v4)) {
                if (udp_v4->connection_id == 0 || now >= udp_v4->expiry) co_await send_connect(*udp_v4);

                auto r4 = co_await send_announce(*udp_v4, peer_id, downloaded, uploaded, total);
                out.peers.insert(out.peers.end(), r4.peers.begin(), r4.peers.end());
                out.interval = r4.interval;
                any_success = true;
            }
        }
        catch (...) {}
    }

    // IPv6 path
    if (udp_v6) {
        try {
            if (co_await ensure_socket_ready(*udp_v6)) {
                if (udp_v6->connection_id == 0 || now >= udp_v6->expiry) co_await send_connect(*udp_v6);

                auto r6 = co_await send_announce(*udp_v6, peer_id, downloaded, uploaded, total);
                out.peers.insert(out.peers.end(), r6.peers.begin(), r6.peers.end());

                if (out.interval == 0 || r6.interval < out.interval) out.interval = r6.interval;

                any_success = true;
            }
        }
        catch (...) {}
    }

    if (any_success) co_return out;
    co_return TrackerResponse{ {}, 180, "UDP tracker unreachable" };
}



void UdpTracker::parse_v4(TrackerResponse& out, std::array<unsigned char, 1500>& response_buf, size_t size) {
        // parse ipv4 peers, 4 bytes -> address, 2 bytes -> port
        for (size_t i{20}; i + 6 <= size; i += 6) {
            auto* peer = response_buf.data() + i;

            boost::asio::ip::address_v4::bytes_type bytes {
                peer[0], peer[1], peer[2], peer[3]
            };
            auto ip = boost::asio::ip::make_address_v4(bytes);
            
            uint16_t port = (peer[4] << 8) | peer[5];
            std::string id = "Unknown";

            out.peers.emplace_back(ip, port, id);
        }
}

void UdpTracker::parse_v6(TrackerResponse& out, std::array<unsigned char, 1500>& response_buf, size_t size) {
        // parse ipv6 peers, 16 bytes -> address, 2 bytes -> port
        for (size_t i{20}; i + 18 <= size; i += 18) {
            auto* peer = response_buf.data() + i;

            boost::asio::ip::address_v6::bytes_type bytes{};
            std::memcpy(bytes.data(), peer, 16);

            auto ip = boost::asio::ip::make_address_v6(bytes);
            
            uint16_t port = (peer[16] << 8) | peer[17];
            std::string id = "Unknown";

            out.peers.emplace_back(ip, port, id);
        }
}