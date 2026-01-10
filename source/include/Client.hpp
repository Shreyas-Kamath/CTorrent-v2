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
    void remove_torrent(const std::string& hash);

    // ui state
    std::vector<TorrentSnapshot> get_torrent_snapshots() const;
    std::vector<PeerSnapshot> get_peer_snapshots(const std::string& hash) const;
    std::vector<TrackerSnapshot> get_tracker_snapshots(const std::string& hash) const;

private:
    // for sessions
    boost::asio::io_context _ioc;
    std::unordered_map<std::string, std::unique_ptr<TorrentSession>> _sessions;
    void detect_network_capabilities();
    bool can_bind_ipv6();

    // helpers
    std::string compute_doc_root() const;
    std::filesystem::path get_exe_dir() const;
    NetworkCapabilities nc;
    uint16_t listen_port = 6881;
};
