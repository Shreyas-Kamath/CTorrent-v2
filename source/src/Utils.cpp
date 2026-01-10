#include "Utils.hpp"

#include <vector>

std::string read_from_file(const std::string& path) {
    std::string input;
	std::ifstream file(path, std::ios::binary | std::ios::ate);

	if (!file.is_open()) {
		throw std::runtime_error("Could not open file: " + path);
	}

	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::string data(size, '\0');
	file.read(data.data(), size);

    return data;
}

bool is_good_ipv6(const IN6_ADDR& addr) {
    // Reject ::1
    static const IN6_ADDR loopback = IN6ADDR_LOOPBACK_INIT;
    if (std::memcmp(&addr, &loopback, sizeof(addr)) == 0)
        return false;

    // Reject link-local fe80::/10
    if (addr.u.Byte[0] == 0xfe && (addr.u.Byte[1] & 0xc0) == 0x80)
        return false;

    // Reject multicast ff00::/8
    if (addr.u.Byte[0] == 0xff)
        return false;

    // Accept global unicast 2000::/3
    if ((addr.u.Byte[0] & 0xe0) == 0x20)
        return true;

    return false;
}

std::optional<boost::asio::ip::address_v6> detect_ipv6_address() {
	ULONG size = 0;
	GetAdaptersAddresses(AF_INET6, 0, nullptr, nullptr, &size);

	std::vector<char> buffer(size);
	auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

	if (GetAdaptersAddresses(AF_INET6, 0, nullptr, adapters, &size) != NO_ERROR) return std::nullopt;

	for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
		if (adapter->OperStatus != IfOperStatusUp) continue;

		for (auto* ua = adapter->FirstUnicastAddress; ua; ua = ua->Next) {
			if (ua->Address.lpSockaddr->sa_family != AF_INET6) continue;

			auto* sa = reinterpret_cast<sockaddr_in6*>(ua->Address.lpSockaddr);

			if (!is_good_ipv6(sa->sin6_addr)) continue;

			char str[INET6_ADDRSTRLEN]{};

			if (!InetNtopA(AF_INET6, &sa->sin6_addr, str, sizeof(str))) continue;

			return boost::asio::ip::make_address_v6(str);
		}
	}

	return std::nullopt;
}