#include "HttpsTracker.hpp"

#include <iostream>

boost::urls::url HttpsTracker::build_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total) {
    boost::urls::url announce_url = _url;

    announce_url.set_encoded_params({
        {"info_hash", _encoded_info_hash},
        {"peer_id", peer_id},
        {"port", "6881"},
        {"downloaded", std::to_string(downloaded)},
        {"uploaded", std::to_string(uploaded)},
        {"left", std::to_string(total - downloaded)},
        {"compact", "1"}
    });

    // if (my_ipv6.has_value()) announce_url.params().set("ipv6", my_ipv6.value());
    if (downloaded == 0) announce_url.params().set("event", "started");

    return announce_url;
}

boost::asio::awaitable<TrackerResponse> HttpsTracker::async_announce(const std::string& peer_id, uint64_t downloaded, uint64_t uploaded, uint64_t total) {
    auto executor = co_await net::this_coro::executor;
    boost::system::error_code ec;
    
    tcp::resolver resolver(executor);

    ssl::stream<tcp::socket> stream(executor, _ssl_ctx);

    if (!SSL_set_tlsext_host_name(stream.native_handle(), _host.c_str())) {
        co_return TrackerResponse{ {}, 180, "Tracker unreachable" };
    }

    auto announce_url = build_announce(peer_id, downloaded, uploaded, total);
    auto target = announce_url.encoded_target();

    http::request<http::string_body> req { http::verb::get, target, 11 };
    req.set(http::field::host, _host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    // async
    auto results = co_await resolver.async_resolve(_host, std::to_string(_port), net::redirect_error(net::use_awaitable, ec));
    if (ec) co_return TrackerResponse{ {}, 180, ec.message() };

    co_await net::async_connect(stream.next_layer(), results, net::redirect_error(net::use_awaitable, ec));
    if (ec) co_return TrackerResponse{ {}, 180, ec.message() };

    co_await stream.async_handshake(ssl::stream_base::client, net::redirect_error(net::use_awaitable, ec));
    if (ec) co_return TrackerResponse{ {}, 180, ec.message() };

    co_await http::async_write(stream, req, net::redirect_error(net::use_awaitable, ec));
    if (ec) co_return TrackerResponse{ {}, 180, ec.message() };

    boost::beast::flat_buffer buffer;
    http::response<http::dynamic_body> res;

    co_await http::async_read(stream, buffer, res, net::redirect_error(net::use_awaitable, ec));

    if (ec) {
        // sometimes trackers behave badly, like closing the stream before the response is complete
        // tolerate it and try to parse what they send
        if (ec == http::error::end_of_stream || ec == net::ssl::error::stream_truncated) { ec = {}; }
        else co_return TrackerResponse{ {}, 180, ec.message() };
    }

    // ignore end of filestream
    co_await stream.async_shutdown(net::redirect_error(net::use_awaitable, ec));
    if (ec) {
        if (ec == http::error::end_of_stream || ec == net::ssl::error::stream_truncated) { ec = {}; }
        else co_return TrackerResponse{ {}, 180, ec.message() };
    }

    std::string body = boost::beast::buffers_to_string(res.body().data());

    // std::cout << "Raw response:\n" << body << '\n';

    auto response = parse_peers(body);

    co_return response;
}

TrackerResponse HttpsTracker::parse_peers(const std::string& body) {
    TrackerResponse out;

    try {
        BEncodeParser resp_parser(body);
        auto root = resp_parser.parse().as_dict();

        const auto& peers_entry = root.at("peers");

        if (root.contains("interval")) {
            out.interval = (uint32_t)root.at("interval").as_int();
        }

        if (peers_entry.is_list()) {
            for (const auto& p : peers_entry.as_list()) {
                const auto& d = p.as_dict();

                auto ip   = d.at("ip").as_string();

                std::string id = "";
                if (d.contains("peer id")) id = d.at("peer id").as_string();

                auto port = d.at("port").as_int();

                out.peers.emplace_back(ip, (int)port, id);
            }
        }

        else if (peers_entry.is_string()) {
            const auto blob = peers_entry.as_string();

            if (blob.size() % 6 != 0) throw std::runtime_error("Invalid compact peer list");

            for (size_t i = 0; i < blob.size(); i += 6) {

                const unsigned char* x = reinterpret_cast<const unsigned char*>(&blob[i]);

                auto ip = std::format("{}.{}.{}.{}", x[0], x[1], x[2], x[3]);
                auto port = (x[4] << 8) | x[5];

                out.peers.emplace_back(ip, (int)port, "");
            }
        }
        else {
            throw std::runtime_error("Unsupported peers format");
        }
    }
    catch (const std::exception& ex) {
        out.error = ex.what();
    }

    return out;
}
