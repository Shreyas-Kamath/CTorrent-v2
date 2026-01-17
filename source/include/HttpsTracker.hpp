#pragma once

#include "BaseTracker.hpp"
#include "NetworkCapabilities.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;

class HttpsTracker : public BaseTracker {
public:    
    HttpsTracker(boost::asio::any_io_executor exec, std::string_view tracker_url, const std::array<unsigned char, 20>& info_hash, const NetworkCapabilities& nc): BaseTracker(exec, tracker_url, info_hash, nc), _ssl_ctx(boost::asio::ssl::context::tlsv12_client) {
        _encoded_info_hash.reserve(_info_hash.size() * 3);

        for (unsigned char b: _info_hash) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", b);
            _encoded_info_hash += buf;
        }

        if (_nc.ipv6_address) {
            auto bytes = _nc.ipv6_address->to_bytes();
            ipv6_raw.reserve(48);
            static constexpr char hex[] = "0123456789ABCDEF";

            for (uint8_t b: bytes) {
                ipv6_raw.push_back('%');
                ipv6_raw.push_back(hex[b >> 4]);
                ipv6_raw.push_back(hex[b & 0x0F]);
            }
        }
    }

    ~HttpsTracker() = default;

    boost::asio::awaitable<TrackerResponse> async_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total) override; 

    boost::urls::url build_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total);

    void stop() override;

private:
    boost::asio::ssl::context _ssl_ctx;
    std::optional<tcp::resolver> _resolver;
    std::optional<ssl::stream<tcp::socket>> _stream;
    std::atomic<bool> _stopped{false};

    TrackerResponse parse_peers(const std::string& body);
    std::string _encoded_info_hash;

    std::string ipv6_raw;

    void parse_v4(TrackerResponse& out, const BEncodeValue& peers_entry);
    void parse_v6(TrackerResponse& out, const BEncodeValue& peers_entry);
};