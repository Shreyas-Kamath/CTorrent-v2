#pragma once

#include "server.hpp"
#include "TorrentSession.hpp"
#include "NetworkCapabilities.hpp"

#include <filesystem>
#include <string>
#include <print>
#include <unordered_map>
#include <memory>

#include <boost/asio.hpp>

struct TorrentSnapshot;
struct PeerSnapshot;
struct TrackerSnapshot;
struct NetworkCapabilities;

struct AddTorrentResult {
    std::string hash;
    std::string name;
    bool success;
    std::string error;
};

class Client {
public:
    Client();
    void run();
    AddTorrentResult add_torrent(const std::vector<char>& data);
    boost::asio::awaitable<void> remove_if_exists(const std::string& hash, bool remove_files);

    // ui state
    std::vector<TorrentSnapshot> get_torrent_snapshots() const;
    std::vector<PeerSnapshot> get_peer_snapshots(const std::string& hash) const;
    std::vector<TrackerSnapshot> get_tracker_snapshots(const std::string& hash) const;

private:
    // for sessions
    boost::asio::io_context _ioc;
    boost::asio::thread_pool _disk_pool{1};

    std::unordered_map<std::string, std::unique_ptr<TorrentSession>> _sessions;
    void detect_network_capabilities();
    bool can_bind_ipv6();
    void start_acceptors();

    boost::asio::awaitable<void> accept_loop_v4();
    boost::asio::awaitable<void> accept_loop_v6();
    boost::asio::awaitable<std::optional<std::array<unsigned char, 20>>> extract_info_hash(boost::asio::ip::tcp::socket& socket);
    std::string compute_info_hash_hex(const std::array<unsigned char, 20>& info_hash) const;

    // acceptors
    std::unique_ptr<boost::asio::ip::tcp::acceptor> v4_acceptor;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> v6_acceptor;

    // helpers
    std::string compute_doc_root() const;
    std::filesystem::path get_exe_dir() const;
    NetworkCapabilities nc;
    uint16_t listen_port = 6881;
};
