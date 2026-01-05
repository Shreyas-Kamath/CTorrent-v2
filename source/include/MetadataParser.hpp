#pragma once

#include "Bencode.hpp"

#include <string>
#include <optional>
#include <array>
#include <ranges>

#include <openssl/sha.h>

struct TorrentFile {
	std::string path;
	uint64_t length;
};

struct Metadata {
    Metadata(const std::vector<char>& data): in(data.begin(), data.end()) {
        parse_torrent();
        compute_info_hash_hex();
    }

    std::string_view announce;
    std::vector<std::vector<std::string_view>> announce_list;
    std::string_view name;
    uint64_t piece_length = 0;
    std::vector<std::array<unsigned char, 20>> piece_hashes;

    std::vector<TorrentFile> files;
    uint64_t total_size = 0;

    std::optional<std::string_view> comment;
    std::optional<std::string_view> created_by;
    uint64_t creation_date = 0;

    std::array<unsigned char, 20> info_hash{};

    std::string info_hash_hex;

private:

    std::string in;

    void parse_torrent();
    void compute_info_hash_hex();
};

