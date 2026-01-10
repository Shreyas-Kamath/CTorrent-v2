#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <string_view>

#include "Peer.hpp"
#include "BEncode.hpp"

#include <boost/url.hpp>
#include <boost/asio.hpp>
#include <boost/url/grammar/charset.hpp>

class TorrentSession;

struct TrackerResponse {

    std::vector<Peer> peers;
    std::optional<uint32_t> interval = std::nullopt;
    std::string error;
};

class BaseTracker {
public:
    BaseTracker(boost::asio::io_context& io, std::string_view tracker_url, const std::array<unsigned char, 20>& info_hash)
        : _io(io), _raw_url(tracker_url), _info_hash(info_hash)
    {
        auto rv = boost::urls::parse_uri(_raw_url);
        if (!rv)
            throw std::runtime_error("Invalid tracker URL");

        _url = *rv;

        _scheme = std::string(_url.scheme());
        _host   = std::string(_url.host());

        if (_url.port().empty()) {
            _port = (_scheme == "udp") ? 6969 : 443;
        } else {
            _port = static_cast<uint16_t>(
                std::stoi(std::string(_url.port()))
            );
        }

        _path = std::string(_url.path());
    }

    virtual ~BaseTracker() = default;

    virtual boost::asio::awaitable<TrackerResponse> async_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total) = 0;

    const std::string_view url() const { return _raw_url; }

    virtual void stop() { }

protected:
    boost::asio::io_context& _io;

    std::string      _raw_url;
    boost::urls::url _url;
    std::string      _scheme;
    std::string      _host;
    uint16_t         _port;
    std::string      _path;

    const std::array<unsigned char, 20>& _info_hash;
};
