#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

#include <boost/asio.hpp>

#include "BaseTracker.hpp"
#include "MetadataParser.hpp"
#include "PieceManager.hpp"
#include "FileManager.hpp"
#include "Utils.hpp"

class PeerConnection;
struct TorrentSnapshot;
struct PeerSnapshot;
struct TrackerSnapshot;
struct NetworkCapabilities;

class TorrentSession {
public:
    TorrentSession(boost::asio::any_io_executor exec, Metadata&& md, const NetworkCapabilities& nc);

    const std::string_view& name() const;

    void start(); 
    void stop();   

    // state for ui updates
    TorrentSnapshot snapshot() const;
    std::vector<PeerSnapshot> peer_snapshots() const;
    std::vector<TrackerSnapshot> tracker_snapshots() const;
    void add_inbound_peer(boost::asio::ip::tcp::socket&& socket, PeerDirection dir);
    
private:
    [[nodiscard]] boost::asio::awaitable<void> remove_peer(const Peer& peer);
    [[nodiscard]] boost::asio::awaitable<void> run_peer(std::shared_ptr<PeerConnection> conn);
    void broadcast_have(uint32_t piece);

    struct TrackerStats {
        std::chrono::steady_clock::time_point next_announce;
        size_t peers_returned{};
        std::string status{};
        bool reachable = false;
        uint32_t interval{};
    };

    struct TrackerState {
        std::shared_ptr<BaseTracker> _tracker_shared_ptr;
        TrackerStats stats;
        boost::asio::steady_timer timer;

        TrackerState(std::shared_ptr<BaseTracker> t, boost::asio::any_io_executor exec): _tracker_shared_ptr(std::move(t)), timer(exec) {}
    };


    bool _stopped = false;

    boost::asio::awaitable<void> tracker_loop(TrackerState& state);

    boost::asio::any_io_executor _exec;

    std::string peer_id = "-TR2940-1234567890ab";
    Metadata _metadata;                                     // parsed metadata and raw string
    std::vector<TrackerState> _tracker_list;
    
    const uint32_t DEFAULT_ANNOUNCE_TIMER = 180;

    PieceManager _pm;
    FileManager _fm;
    const NetworkCapabilities& _nc;

    void build_tracker_list();
    void on_tracker_response(const TrackerResponse& resp);

    struct PeerHash {
        size_t operator()(const Peer& p) const noexcept {
            size_t h{};

            const auto& addr = p.addr();

            if (addr.is_v4()) {
                auto bytes = addr.to_v4().to_bytes();
                h = hash_bytes(bytes.data(), bytes.size());
            }
            else if (addr.is_v6()) {
                auto bytes = addr.to_v6().to_bytes();
                h = hash_bytes(bytes.data(), bytes.size());
            }

            h ^= std::hash<int>{}(p.port()) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }
    };

    static size_t hash_bytes(const uint8_t* data, size_t len) noexcept;

    std::unordered_map<Peer, std::shared_ptr<PeerConnection>, PeerHash> _peer_connections;
};
