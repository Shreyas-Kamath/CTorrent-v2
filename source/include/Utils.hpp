#pragma once

#include <string>
#include <stdexcept>
#include <fstream>

std::string read_from_file(const std::string& path);

std::string compute_info_hash_hex();