#pragma once

#include <optional>

#include <boost/asio/ip/address_v6.hpp>

struct NetworkCapabilities {
    bool ipv4_inbound = true, ipv4_outbound = true;
    bool ipv6_inbound = false, ipv6_outbound = false;

    std::optional<boost::asio::ip::address_v6> ipv6_address;
};

