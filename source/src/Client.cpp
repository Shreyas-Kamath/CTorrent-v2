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
    auto exe_dir  = get_exe_dir();
    auto doc_root = compute_doc_root();

    HttpServer server(_ioc, 8080, doc_root, this);
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

    // detect ipv6
    // auto ipv6 = detect_ipv6_address();

    // spawn a session
    auto session = std::make_unique<TorrentSession>(_ioc, std::move(md));

    session->start();

    _sessions[hash] = std::move(session);

    return { hash, std::string(_sessions[hash]->name()), true, "Torrent added" };
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