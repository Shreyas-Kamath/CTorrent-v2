#pragma once

#include <boost/asio.hpp>

class Peer {
public:
    Peer(boost::asio::ip::address ip, int port, std::string_view id): _ip(ip), _port(port), _id(id) {}

    const boost::asio::ip::address& addr() const { return _ip; }
    const int port() const { return _port; }

    bool operator==(const Peer& other) const noexcept {
        return _ip == other._ip && _port == other._port;
    }

    std::string& id() { return _id; }

private:
    boost::asio::ip::address _ip;
    int _port;
    std::string _id;
};