#include "Utils.hpp"

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