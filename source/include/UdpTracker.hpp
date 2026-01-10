#pragma once

#include "BaseTracker.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>

namespace net = boost::asio;
using udp = net::ip::udp;

class UdpTracker: public BaseTracker {
public:
    UdpTracker(boost::asio::io_context& ioc, std::string_view tracker_url, const std::array<unsigned char, 20>& info_hash): BaseTracker(ioc, tracker_url, info_hash), _socket(_io) 
    {}

    boost::asio::awaitable<TrackerResponse> async_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total) override; 

private:

    udp::endpoint _endpoint;
    udp::socket _socket;
    bool connected = false;

    uint64_t _connection_id{};
    std::chrono::steady_clock::time_point _conn_expiry;

    boost::asio::awaitable<bool> ensure_socket_ready();
    static uint32_t random_u32();
    boost::asio::awaitable<bool> send_connect();
    boost::asio::awaitable<TrackerResponse> send_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total);
};