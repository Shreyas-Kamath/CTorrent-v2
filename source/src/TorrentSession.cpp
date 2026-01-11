#include "TorrentSession.hpp"
#include "TrackerFactory.hpp"
#include "PeerConnection.hpp"
#include "TorrentSnapshot.hpp"
#include "PeerSnapshot.hpp"
#include "TrackerSnapshot.hpp"

#include <iostream>
#include <ranges>
#include <filesystem>
#include <print>

const std::string_view& TorrentSession::name() const { return _metadata.name; }

TorrentSession::TorrentSession(boost::asio::any_io_executor exec, Metadata&& md): 
    _exec(exec), 
    _metadata(std::move(md)),
    _fm(std::filesystem::current_path(), _metadata.name, _metadata.files, _metadata.total_size, _metadata.piece_length, _exec),
    _pm(_exec, _metadata.piece_hashes.size(), _metadata.piece_length, _metadata.total_size, _metadata.piece_hashes, _fm, [this](uint32_t piece) { broadcast_have(piece); })
    {
        build_tracker_list();
    }

void TorrentSession::on_tracker_response(const TrackerResponse& resp) {
    if (!resp.error.empty()) std::println("Warning: {}", resp.error);
    
    std::println("Got {} peers", resp.peers.size());

    for (const auto& peer: resp.peers) {
        auto exists = std::ranges::any_of(_peer_connections, [&peer](const auto& conn) {
            return conn->peer() == peer;
        });

        if (!exists) {
            auto conn = std::make_shared<PeerConnection>(
                _exec, peer, _metadata.info_hash, peer_id, _metadata.piece_hashes.size(), _pm
            );

            _peer_connections.push_back(conn);
            boost::asio::co_spawn(_exec, run_peer(conn), boost::asio::detached);
        }
    }
}

void TorrentSession::start() {
    for (auto& state: _tracker_list) boost::asio::co_spawn(_exec, tracker_loop(state), boost::asio::detached);
}

void TorrentSession::stop() {}

boost::asio::awaitable<void> TorrentSession::remove_peer(std::shared_ptr<PeerConnection> peer) {
    std::erase(_peer_connections, peer);
    co_return;
}

void TorrentSession::broadcast_have(uint32_t piece) {
    for (auto& peer: _peer_connections) {

        if (!peer || peer->is_stopped()) continue;

        boost::asio::co_spawn(
            _exec,
            peer->send_have(piece),
            boost::asio::detached
        );
    }
}

boost::asio::awaitable<void> TorrentSession::tracker_loop(TrackerState& state) {
    while (!_stopped) {
        try {
            auto resp = co_await state._tracker_shared_ptr->async_announce(peer_id, _pm.downloaded_bytes(), _pm.uploaded_bytes(), _pm.total_bytes());
            on_tracker_response(resp);

            state.stats.peers_returned = resp.peers.size();
            state.stats.interval = resp.interval.value_or(DEFAULT_ANNOUNCE_TIMER);

            state.stats.status = resp.error;
            if (!resp.error.empty()) state.stats.reachable = false;
            else state.stats.reachable = true;

            state.stats.next_announce =
                std::chrono::steady_clock::now() +
                std::chrono::seconds(state.stats.interval);
        }
        catch (const std::exception& e) {
            state.stats.reachable = false;
            state.stats.status = e.what();
            state.stats.peers_returned = 0;
            state.stats.interval = DEFAULT_ANNOUNCE_TIMER;

            state.stats.next_announce =
                std::chrono::steady_clock::now() +
                std::chrono::seconds(DEFAULT_ANNOUNCE_TIMER);
        }

        state.timer.expires_at(state.stats.next_announce);
        co_await state.timer.async_wait(boost::asio::use_awaitable);
    }
}

void TorrentSession::build_tracker_list() {
    std::unordered_set<std::string_view> seen;
    
    auto add = [&](std::string_view url) {
        if (!url.empty() && !seen.contains(url)) {
            seen.insert(url);
            _tracker_list.emplace_back(make_tracker(_exec, url, _metadata.info_hash), _exec);
        }
    };

    add(_metadata.announce);

    for (const auto& tier : _metadata.announce_list) {
        for (const auto& url : tier) add(url);
    }
}

TorrentSnapshot TorrentSession::snapshot() const {
    TorrentSnapshot cs;

    cs.name = _metadata.name;
    cs.hash = _metadata.info_hash_hex;
    cs.total_size = _metadata.total_size;

    cs.downloaded = _pm.downloaded_bytes();
    cs.uploaded = _pm.uploaded_bytes();

    cs.progress = (double)cs.downloaded * 100.0 / cs.total_size;

    cs.peers = _peer_connections.size();
    cs.trackers = _tracker_list.size();

    cs.status = _pm.is_complete() ? "completed" : _stopped ? "paused" : "downloading";

    return cs;
}

std::vector<PeerSnapshot> TorrentSession::peer_snapshots() const {
    std::vector<PeerSnapshot> out;
    out.reserve(_peer_connections.size());

    for (const auto& conn: _peer_connections) {
        PeerSnapshot ps;

        ps.ip = conn->peer().addr().to_string();
        ps.name = conn->peer().id();
        ps.progress = conn->progress();
        ps.requests = conn->requests();
        ps.choked = conn->choked();
        ps.interested = conn->interested();

        out.push_back(std::move(ps));
    }

    return out;
}

std::vector<TrackerSnapshot> TorrentSession::tracker_snapshots() const {
    std::vector<TrackerSnapshot> out;
    out.reserve(_tracker_list.size());

    for (const auto& [ptr, stats, timer]: _tracker_list) {
        TrackerSnapshot ts;
        
        ts.url = std::string(ptr->url());
        ts.peers_returned = (uint32_t)stats.peers_returned;
        ts.interval = stats.interval;
        ts.reachable = stats.reachable;
        ts.status = stats.status;
        
        // calculate next announce
        auto delta = stats.next_announce - std::chrono::steady_clock::now();
        ts.next_in = delta > std::chrono::seconds(0) ? (uint32_t)duration_cast<std::chrono::seconds>(delta).count(): 0;

        out.push_back(std::move(ts));
    }

    return out;
}

void TorrentSession::add_inbound_peer(boost::asio::ip::tcp::socket&& socket) {
    // auto conn = std::make_shared<PeerConnection>(
    //     std::move(socket), _metadata.info_hash, peer_id, _metadata.piece_hashes.size(), _pm
    // );

    // conn->start_inbound();
    // _peer_connections.push_back(conn);
}

boost::asio::awaitable<void> TorrentSession::run_peer(std::shared_ptr<PeerConnection> conn) {
    co_await conn->start();
    co_await remove_peer(conn);
}