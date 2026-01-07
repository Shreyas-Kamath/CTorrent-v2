#pragma once

#include "BaseTracker.hpp"

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
    HttpsTracker(boost::asio::io_context& ioc, std::string_view tracker_url, const std::array<unsigned char, 20>& info_hash, std::optional<std::string> ipv6): BaseTracker(ioc, tracker_url, info_hash, ipv6), _ssl_ctx(boost::asio::ssl::context::tlsv12_client) {
        _encoded_info_hash.reserve(_info_hash.size() * 3);

        for (unsigned char b: _info_hash) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", b);
            _encoded_info_hash += buf;
        }
    }

    ~HttpsTracker() = default;

    boost::asio::awaitable<TrackerResponse> async_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total) override; 

    boost::urls::url build_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total);

private:
    boost::asio::ssl::context _ssl_ctx;
    TrackerResponse parse_peers(const std::string& body);
    std::string _encoded_info_hash;

};