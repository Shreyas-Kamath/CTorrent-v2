#pragma once

#include <string>
#include <stdexcept>
#include <fstream>
#include <optional>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

std::string read_from_file(const std::string& path);
std::optional<std::string> detect_ipv6_address();
bool is_good_ipv6(const IN6_ADDR& addr);