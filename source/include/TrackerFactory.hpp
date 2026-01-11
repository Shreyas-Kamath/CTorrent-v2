#include <memory>
#include <string_view>
#include <stdexcept>

#include <boost/asio.hpp>

class BaseTracker;

std::shared_ptr<BaseTracker> make_tracker(boost::asio::any_io_executor exec, const std::string_view url, const std::array<unsigned char, 20>& info_hash);