#include "TrackerFactory.hpp"
#include "HttpsTracker.hpp"
#include "UdpTracker.hpp"

std::shared_ptr<BaseTracker> make_tracker(boost::asio::io_context& ioc, const std::string_view url, const std::array<unsigned char, 20>& info_hash, std::optional<std::string> ipv6) {
    // if (url.starts_with("http://")) return std::make_unique<HttpTracker>(url);
    if (url.starts_with("https://")) return std::make_shared<HttpsTracker>(ioc, url, info_hash, ipv6);
    else if (url.starts_with("udp://")) return std::make_shared<UdpTracker>(ioc, url, info_hash, ipv6);
    // throw std::invalid_argument("Unsupported tracker URL: " + url);
}
