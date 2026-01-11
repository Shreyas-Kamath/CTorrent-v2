#include "TrackerFactory.hpp"
#include "HttpsTracker.hpp"
#include "UdpTracker.hpp"

std::shared_ptr<BaseTracker> make_tracker(boost::asio::any_io_executor exec, const std::string_view url, const std::array<unsigned char, 20>& info_hash) {
    // if (url.starts_with("http://")) return std::make_unique<HttpTracker>(url);
    if (url.starts_with("https://")) return std::make_shared<HttpsTracker>(exec, url, info_hash);
    else if (url.starts_with("udp://")) return std::make_shared<UdpTracker>(exec, url, info_hash);
    // throw std::invalid_argument("Unsupported tracker URL: " + url);
}
