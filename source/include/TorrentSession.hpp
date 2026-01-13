#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_set>

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
    [[nodiscard]] boost::asio::awaitable<void> remove_peer(std::shared_ptr<PeerConnection>);
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

    std::vector<std::shared_ptr<PeerConnection>> _peer_connections;
};
