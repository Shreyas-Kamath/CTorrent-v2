#include "Client.hpp"
#include "MetadataParser.hpp"
#include "TorrentSnapshot.hpp"
#include "PeerSnapshot.hpp"
#include "TrackerSnapshot.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include "Utils.hpp"

Client::Client() = default;

void Client::run() {
    detect_network_capabilities();
    start_acceptors();

    auto exe_dir  = get_exe_dir();
    auto doc_root = compute_doc_root();

    HttpServer server(_ioc.get_executor(), 8080, doc_root, this);
    server.run();
    _ioc.run();
}

std::filesystem::path Client::get_exe_dir() const {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
#else
    return std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
}

std::string Client::compute_doc_root() const {
    auto exe_dir = get_exe_dir();
    return (exe_dir / ".." / ".." / "webui")
        .lexically_normal()
        .string();
}

AddTorrentResult Client::add_torrent(const std::vector<char>& data) {
    Metadata md(data);

    auto hash = md.info_hash_hex;

    if (_sessions.contains(hash)) return { hash, std::string(md.name), false, "Torrent already exists" };

    // spawn a session
    auto session = std::make_unique<TorrentSession>(_ioc.get_executor(), _disk_pool.get_executor(), std::move(md), nc);

    session->start();

    _sessions[hash] = std::move(session);

    return { hash, std::string(_sessions[hash]->name()), true, "Torrent added" };
}

boost::asio::awaitable<void> Client::remove_if_exists(const std::string& hash, bool remove_files) {
    auto it = _sessions.find(hash);

    if (it == _sessions.end()) co_return;
    co_await it->second->stop();
    _sessions.erase(hash);

    co_return;
}

std::vector<TorrentSnapshot> Client::get_torrent_snapshots() const {

    auto out = _sessions 
    | std::views::values 
    | std::views::transform([](auto&& session) {
        return session->snapshot();
    }) 
    | std::ranges::to<std::vector<TorrentSnapshot>>();

    return out;
}

std::vector<PeerSnapshot> Client::get_peer_snapshots(const std::string& hash) const {
    return _sessions.at(hash)->peer_snapshots();
}

std::vector<TrackerSnapshot> Client::get_tracker_snapshots(const std::string& hash) const {
    return _sessions.at(hash)->tracker_snapshots();
}

void Client::detect_network_capabilities() {
    nc.ipv6_address = detect_ipv6_address();
    nc.ipv6_outbound = nc.ipv6_address.has_value();
    
    if (nc.ipv6_outbound) nc.ipv6_inbound = can_bind_ipv6();

    std::println("ipv6 outbound: {}", nc.ipv6_outbound);
    std::println("ipv6 inbound: {}", nc.ipv6_inbound);
}

void Client::start_acceptors() {
    using tcp = boost::asio::ip::tcp;
    boost::system::error_code ec;

    if (nc.ipv4_inbound) {
        v4_acceptor = std::make_unique<tcp::acceptor>(_ioc);
        v4_acceptor->open(tcp::v4(), ec);

        if (!ec) {
            v4_acceptor->bind({tcp::v4(), listen_port}, ec);
            if (!ec) v4_acceptor->listen(50, ec);
        }
    }

    if (ec) {
        std::println("ipv4 acceptor failed: {}", ec.message());
        v4_acceptor.reset();
    }
    else {
        std::println("ipv4 acceptor is listening on {}", listen_port);
        boost::asio::co_spawn(_ioc, accept_loop_v4(), boost::asio::detached);
    }

    ec.clear();

    if (nc.ipv6_inbound) {
        v6_acceptor = std::make_unique<tcp::acceptor>(_ioc);
        v6_acceptor->open(tcp::v6(), ec);
        v6_acceptor->set_option(boost::asio::ip::v6_only(true), ec);
        if (!ec) {
            v6_acceptor->bind({tcp::v6(), listen_port}, ec);
            if (!ec) v6_acceptor->listen(50, ec);
        }
    }

    if (ec) {
        std::println("ipv6 acceptor failed: {}", ec.message());
        v6_acceptor.reset();
    }
    else {
        std::println("ipv6 acceptor is listening on {}", listen_port);
        boost::asio::co_spawn(_ioc, accept_loop_v6(), boost::asio::detached);
    }
}

bool Client::can_bind_ipv6() {
    using tcp = boost::asio::ip::tcp;

    boost::system::error_code ec;
    tcp::acceptor acc(_ioc);

    acc.open(tcp::v6(), ec);
    if (ec) {
        std::println("Could not open ipv6 acceptor: {}", ec.message());
        return false;
    }

    acc.set_option(boost::asio::ip::v6_only(true), ec);
    if (ec) return false;

    acc.bind({tcp::v6(), listen_port}, ec);
    if (ec) {
        std::println("Could not bind ipv6 to listen port: {}", ec.message());
        return false;
    }

    acc.close(ec);
    return true;
}

boost::asio::awaitable<void> Client::accept_loop_v4() {
    using tcp = boost::asio::ip::tcp;

    while (true) {
        if (!v4_acceptor || !v4_acceptor->is_open()) co_return;
        tcp::socket socket(_ioc);
        
        boost::system::error_code ec;
        co_await v4_acceptor->async_accept(socket, boost::asio::redirect_error(boost::asio::use_awaitable, ec));

        // detach later
        if (!ec) {
            auto ep = socket.remote_endpoint();
            boost::asio::co_spawn(_ioc.get_executor(), handle_inbound(std::move(socket), ep), boost::asio::detached);
        }
    }
}

boost::asio::awaitable<void> Client::accept_loop_v6() {
    using tcp = boost::asio::ip::tcp;

    // detach later
    while (true) {
        if (!v6_acceptor || !v6_acceptor->is_open()) co_return;
        tcp::socket socket(_ioc);
        
        boost::system::error_code ec;
        co_await v6_acceptor->async_accept(socket, boost::asio::redirect_error(boost::asio::use_awaitable, ec));

        if (!ec) {
            auto ep = socket.remote_endpoint();
            boost::asio::co_spawn(_ioc.get_executor(), handle_inbound(std::move(socket), ep), boost::asio::detached);
        }
    }
}

boost::asio::awaitable<void> Client::handle_inbound(boost::asio::ip::tcp::socket socket, boost::asio::ip::tcp::endpoint ep) {
            auto extracted = co_await extract_info_hash(socket);
            auto hexed_hash = extracted.and_then([this](const auto& h) {
                return std::optional<std::string>{ compute_info_hash_hex(h.first) };
            });

            if (!hexed_hash) {
                socket.close();
                co_return;
            }

            auto it = _sessions.find(*hexed_hash);

            // it is possible that a peer from a stale or previously downloaded torrent is trying to connect
            // through someone else's peer list, we cannot serve requests here
            if (it == _sessions.end()) {
                socket.close();
                co_return;
            }
            // find
            
            // add the peer now
            co_await it->second->add_inbound_peer(std::move(socket), ep, PeerDirection::Inbound, std::move(extracted->second));
}

boost::asio::awaitable<std::optional<std::pair<std::array<unsigned char, 20>, std::string>>> Client::extract_info_hash(boost::asio::ip::tcp::socket& socket) {
    std::array<unsigned char, 68> buf{};

    boost::system::error_code ec;
    co_await boost::asio::async_read(socket, boost::asio::buffer(buf), boost::asio::redirect_error(boost::asio::use_awaitable, ec));

    if (ec) {
        std::println("Could not read data from incoming socket: {}", socket.remote_endpoint().address().to_string());
        co_return std::nullopt;
    }

    if (buf[0] != 19) co_return std::nullopt;
    // std::println("Pstrlen is valid");

    static constexpr char protocol[] = "BitTorrent protocol";

    for (size_t i = 0; i < 19; ++i) if (static_cast<char>(buf[i + 1]) != protocol[i]) co_return std::nullopt;

    std::array<unsigned char, 20> info_hash{};
    std::copy(buf.begin() + 28, buf.begin() + 48, info_hash.begin());

    const unsigned char* peer_id = buf.data() + 48;
    auto id = decode_peer_id(std::string_view(reinterpret_cast<const char*>(peer_id), 20));

    co_return std::make_pair(info_hash, id);
}

std::string Client::decode_peer_id(std::string_view pid) {
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

std::string Client::compute_info_hash_hex(const std::array<unsigned char, 20>& info_hash) const {
    static const char* hex = "0123456789abcdef";
    std::string info_hash_hex; info_hash_hex.resize(40);

    for (size_t i = 0; i < 20; ++i) {
        unsigned char b = info_hash[i];
        info_hash_hex[2*i]     = hex[(b >> 4) & 0xF];
        info_hash_hex[2*i + 1] = hex[b & 0xF];
    }

    return info_hash_hex;
}