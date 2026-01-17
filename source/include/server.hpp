#pragma once

#include <string>
#include <vector>
#include <memory>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>

namespace http = boost::beast::http;

class Client;

struct UploadedFile {
    std::string filename;
    std::vector<char> data;
    bool valid = false;
};

class HttpServer : public std::enable_shared_from_this<HttpServer> {
public:
    HttpServer(boost::asio::any_io_executor ioc, unsigned short port, std::string doc_root, Client* client);

    void run(); // start accepting connections asynchronously

private:
    // connection handler coroutine
    boost::asio::awaitable<void> accept_loop(boost::asio::ip::tcp::acceptor acceptor);
    boost::asio::awaitable<void> handle_connection(boost::asio::ip::tcp::socket socket);

    // request handling
    void handle_request(const http::request<http::dynamic_body>& req,
                        http::response<http::string_body>& res);

    UploadedFile parse_multipart(const http::request<http::dynamic_body>& req);
    std::string get_boundary(const http::request<http::dynamic_body>& req);
    std::string mime_type(const std::string& path);
    std::string load_file(const std::string& path);

    void handle_static(const http::request<http::dynamic_body>& req,
                       http::response<http::string_body>& res);
    void handle_api(const http::request<http::dynamic_body>& req,
                    http::response<http::string_body>& res);
    void handle_add_torrent(const http::request<http::dynamic_body>& req,
                            http::response<http::string_body>& res);
    void handle_delete_torrent(const http::request<http::dynamic_body>& req,
                            http::response<http::string_body>& res, const std::string& hash);                        
    void fetch_torrents_info(const http::request<http::dynamic_body>& req,
                            http::response<http::string_body>& res);
    void fetch_peers_info(const http::request<http::dynamic_body>& req,
                            http::response<http::string_body>& res, const std::string& hash);
    void fetch_trackers_info(const http::request<http::dynamic_body>& req,
                            http::response<http::string_body>& res, const std::string& hash);

private:
    boost::asio::any_io_executor _exec;
    unsigned short _port;
    std::string _doc_root;
    Client* _client;
};
