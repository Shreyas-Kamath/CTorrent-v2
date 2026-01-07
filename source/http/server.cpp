#include "server.hpp"
#include "Client.hpp"
#include "TorrentSnapshot.hpp"
#include "PeerSnapshot.hpp"
#include "TrackerSnapshot.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/beast.hpp>
#include <fstream>
#include <iostream>
#include <filesystem>

using tcp = boost::asio::ip::tcp;
using namespace boost::asio;
using namespace boost::asio::experimental::awaitable_operators;

namespace http = boost::beast::http;
namespace net = boost::asio;

HttpServer::HttpServer(boost::asio::io_context& ioc, unsigned short port, std::string doc_root, Client* client)
    : _ioc(ioc), _port(port), _doc_root(std::move(doc_root)), _client(client) {}

// -------------------- utility functions --------------------

std::string HttpServer::load_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string HttpServer::mime_type(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".css"))  return "text/css";
    if (path.ends_with(".js"))   return "application/javascript";
    if (path.ends_with(".png"))  return "image/png";
    if (path.ends_with(".jpg"))  return "image/jpeg";
    return "application/octet-stream";
}

std::string HttpServer::get_boundary(const http::request<http::dynamic_body>& req) {
    auto it = req.find(http::field::content_type);
    if (it == req.end()) return {};
    std::string ct = it->value();
    std::string key = "boundary=";
    size_t pos = ct.find(key);
    if (pos == std::string::npos) return {};
    return ct.substr(pos + key.size());
}

UploadedFile HttpServer::parse_multipart(const http::request<http::dynamic_body>& req) {
    UploadedFile result;
    std::string boundary = get_boundary(req);
    if (boundary.empty()) return result;

    std::string body = boost::beast::buffers_to_string(req.body().data());
    std::string marker = "--" + boundary;
    size_t pos = 0;

    while ((pos = body.find(marker, pos)) != std::string::npos) {
        size_t header_end = body.find("\r\n\r\n", pos);
        if (header_end == std::string::npos) break;

        std::string header = body.substr(pos, header_end - pos);
        size_t data_start = header_end + 4;
        size_t next_marker = body.find(marker, data_start);
        if (next_marker == std::string::npos) break;

        std::string part_data = body.substr(data_start, next_marker - data_start);

        if (header.find("name=\"torrent\"") != std::string::npos) {
            size_t fn = header.find("filename=\"");
            if (fn != std::string::npos) {
                fn += 10;
                size_t fn_end = header.find("\"", fn);
                result.filename = header.substr(fn, fn_end - fn);
            }
            result.data.assign(part_data.begin(), part_data.end());
            result.valid = true;
        }

        pos = next_marker;
    }

    return result;
}

// -------------------- request handling --------------------

void HttpServer::handle_request(const http::request<http::dynamic_body>& req,
                                http::response<http::string_body>& res) {
    std::string target = req.target();

    if (target.starts_with("/api/"))
        return handle_api(req, res);

    handle_static(req, res);
}

void HttpServer::handle_static(const http::request<http::dynamic_body>& req,
                               http::response<http::string_body>& res) {
    std::string target = req.target();
    if (target == "/") target = "/index.html";

    std::string path = _doc_root + target;
    std::string body = load_file(path);

    if (body.empty()) {
        res.result(http::status::not_found);
        res.body() = "File not found";
        res.set(http::field::content_type, "text/plain");
        res.prepare_payload();
        return;
    }

    res.result(http::status::ok);
    res.body() = body;
    res.set(http::field::content_type, mime_type(path));
    res.prepare_payload();
}

void HttpServer::handle_api(const http::request<http::dynamic_body>& req, 
                            http::response<http::string_body>& res) {

    auto args = req.target() | std::views::split('/') | std::ranges::to<std::vector<std::string>>();

    if (req.method() == http::verb::post) {
        if (req.target() == "/api/torrents/add") return handle_add_torrent(req, res);
        // if (args.back() == "remove") return handle_delete_torrent(req, res, args[3]);
    }

    if (req.method() == http::verb::get) {
        if (req.target() == "/api/torrents") return fetch_torrents_info(req, res);
        if (args.back() == "peers") return fetch_peers_info(req, res, args[3]); // hash
        if (args.back() == "trackers") return fetch_trackers_info(req, res, args[3]); // hash
    }

    std::println("{}", args);

    res.result(http::status::not_found);
    res.body() = R"({"status":"error","message":"Unknown API endpoint"})";
    res.set(http::field::content_type, "application/json");
    res.prepare_payload();
}

void HttpServer::fetch_peers_info(const http::request<http::dynamic_body>& req,
                        http::response<http::string_body>& res, const std::string& hash) {

    auto peer_snapshots = _client->get_peer_snapshots(hash);

    boost::json::array arr; arr.reserve(peer_snapshots.size());

    for (const auto& snapshot: peer_snapshots) {
        boost::json::object obj;

        obj["ip"] = snapshot.ip;
        obj["version"] = snapshot.name;
        obj["progress"] = snapshot.progress;
        obj["requests"] = snapshot.requests;
        obj["choked"] = snapshot.choked;
        obj["interested"] = snapshot.interested;

        arr.push_back(std::move(obj));
    }

    res.result(http::status::ok);
    res.set(http::field::content_type, "application/json");
    res.body() = boost::json::serialize(arr);
    res.prepare_payload();   
}

void HttpServer::fetch_trackers_info(const http::request<http::dynamic_body>& req,
                        http::response<http::string_body>& res, const std::string& hash) {

    auto tracker_snapshots = _client->get_tracker_snapshots(hash);

    boost::json::array arr; arr.reserve(tracker_snapshots.size());

    for (const auto& snapshot: tracker_snapshots) {
        boost::json::object obj;

        obj["url"] = snapshot.url;
        obj["peers"] = snapshot.peers_returned;
        obj["interval"] = snapshot.interval;
        obj["reachable"] = snapshot.reachable;
        obj["status"] = snapshot.status;
        obj["timer"] = snapshot.next_in;

        arr.push_back(std::move(obj));
    }

    res.result(http::status::ok);
    res.set(http::field::content_type, "application/json");
    res.body() = boost::json::serialize(arr);
    res.prepare_payload();   

}

void HttpServer::handle_add_torrent(const http::request<http::dynamic_body>& req,
                                    http::response<http::string_body>& res) {
    auto file = parse_multipart(req);

    if (!file.valid) {
        res.result(http::status::bad_request);
        res.body() = R"({"status":"error","message":"Invalid upload"})";
        res.set(http::field::content_type, "application/json");
        res.prepare_payload();
        return;
    }

    auto result = _client->add_torrent(file.data);

    boost::json::object obj;
    obj["status"]  = result.success ? "ok" : "error";
    obj["hash"]    = result.hash;
    obj["name"]    = result.name;
    obj["message"] = result.error;

    res.result(http::status::ok);
    res.body() = boost::json::serialize(obj);
    res.set(http::field::content_type, "application/json");
    res.prepare_payload();
}

void HttpServer::fetch_torrents_info(const http::request<http::dynamic_body>& req, http::response<http::string_body>& res) {
    boost::json::array arr;

    auto torrent_snapshots = _client->get_torrent_snapshots();

    for (const auto& snapshot: torrent_snapshots) {
        boost::json::object obj;

        obj["name"] = snapshot.name;
        obj["hash"] = snapshot.hash;
        obj["progress"] = snapshot.progress;
        obj["size"] = snapshot.total_size;
        obj["status"] = snapshot.status;
        obj["peers"] = snapshot.peers;
        obj["trackers"] = snapshot.trackers;

        arr.push_back(std::move(obj));
    }

    res.result(http::status::ok);
    res.set(http::field::content_type, "application/json");
    res.body() = boost::json::serialize(arr);
    res.prepare_payload();    
}

// -------------------- async accept loop --------------------

awaitable<void> HttpServer::accept_loop(tcp::acceptor acceptor) {
    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
        co_spawn(_ioc, handle_connection(std::move(socket)), detached);
    }
}

awaitable<void> HttpServer::handle_connection(tcp::socket socket) {
    try {
        boost::beast::flat_buffer buffer;
        http::request<http::dynamic_body> req;

        co_await http::async_read(socket, buffer, req, use_awaitable);

        http::response<http::string_body> res;
        handle_request(req, res);

        co_await http::async_write(socket, res, use_awaitable);

        boost::system::error_code ec;
        socket.shutdown(tcp::socket::shutdown_send, ec);
    }
    catch (const std::exception& e) {
        std::cerr << "Connection error: " << e.what() << "\n";
    }
}

// -------------------- entry point --------------------

void HttpServer::run() {
    tcp::acceptor acceptor(_ioc, tcp::endpoint(tcp::v4(), _port));
    std::println("Running at http://localhost:{}", _port);

    co_spawn(_ioc, accept_loop(std::move(acceptor)), detached);
}
