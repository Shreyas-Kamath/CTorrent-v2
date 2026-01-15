#pragma once

#include "BaseTracker.hpp"
#include "NetworkCapabilities.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>

#include <concepts>

// concepts
template <typename T>
concept UnsignedNum = std::is_unsigned_v<T> && std::is_integral_v<T>;

template <typename T>
concept MutableBuffer = requires(T t) { requires std::same_as<decltype(*t.data()), unsigned char&>; };

namespace net = boost::asio;
using udp = net::ip::udp;

struct NetworkCapabilities;

class UdpTracker: public BaseTracker {
public:
    UdpTracker(boost::asio::any_io_executor exec, std::string_view tracker_url, const std::array<unsigned char, 20>& info_hash, const NetworkCapabilities& nc): BaseTracker(exec, tracker_url, info_hash, nc) {
        udp_v4.emplace(_exec, udp::v4());
        if (_nc.ipv6_outbound) udp_v6.emplace(_exec, udp::v6());
    }

    boost::asio::awaitable<TrackerResponse> async_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total) override; 

private:

    struct UdpContext {
        udp::socket socket;
        udp::endpoint endpoint;
        udp proto;
        uint64_t connection_id{};   
        std::chrono::steady_clock::time_point expiry{};
        bool connected = false;
        bool bound = false;

        explicit UdpContext(net::any_io_executor exec, udp protocol): socket(exec, protocol), proto(protocol) {}
    };

    std::optional<UdpContext> udp_v4;
    std::optional<UdpContext> udp_v6;

    boost::asio::awaitable<bool> ensure_socket_ready(UdpContext&);
    static uint32_t random_u32();
    boost::asio::awaitable<bool> send_connect(UdpContext&);
    boost::asio::awaitable<TrackerResponse> send_announce(UdpContext& udp, const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total);

    void parse_v4(TrackerResponse& out, std::array<unsigned char, 1500>& response_buf, size_t size);
    void parse_v6(TrackerResponse& out, std::array<unsigned char, 1500>& response_buf, size_t size);

    template <MutableBuffer Buffer, UnsignedNum Value>
    auto write_buffer(Buffer& buf, Value val, size_t& Offset) -> void {
        boost::endian::native_to_big_inplace<Value>(val);
        std::memcpy(buf.data() + Offset, &val, sizeof(Value));
        Offset += sizeof(Value);
    } 
};