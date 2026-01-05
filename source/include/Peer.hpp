#pragma once

#include <boost/asio.hpp>

class Peer {
public:
    Peer(std::string_view ip, int port, std::string_view id): _ip(boost::asio::ip::make_address(ip)), _port(port), _id(id) {}

    const boost::asio::ip::address& addr() const { return _ip; }
    const std::string ip() const { return _ip.to_string(); }
    const int port() const { return _port; }

    bool operator==(const Peer& other) const noexcept {
        return _ip == other._ip;
    }

    std::string& id() { return _id; }

private:
    boost::asio::ip::address _ip;
    int _port;
    std::string _id;
};