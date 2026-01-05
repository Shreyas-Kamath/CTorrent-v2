#include "MetadataParser.hpp"

void Metadata::parse_torrent() {

	BEncodeParser parser(in);

	auto dict = parser.parse().as_dict();

    // Required: announce
    auto it = dict.find("announce");
    if (it != dict.end() && it->second.is_string()) announce = it->second.as_string();

    // Optional: announce-list
    int tiers{ 1 };

    it = dict.find("announce-list");
    if (it != dict.end() && it->second.is_list()) {
        for (const auto& tier_val : it->second.as_list()) {
            std::vector<std::string_view> tier;
            if (tier_val.is_list()) {
                for (const auto& tracker : tier_val.as_list()) {
                    if (tracker.is_string()) tier.push_back(tracker.as_string());
                }
            }
            ++tiers;
            if (!tier.empty()) announce_list.push_back(std::move(tier));
        }
    }

    // Info dictionary (required)
    it = dict.find("info");
    if (it == dict.end() || !it->second.is_dict())
        throw std::runtime_error("Missing info dictionary");
    const auto& info = it->second.as_dict();

    // Name
    auto name_it = info.find("name");
    if (name_it != info.end() && name_it->second.is_string()) {
        name = name_it->second.as_string();
    }
       
    // Piece length
    auto pl_it = info.find("piece length");
    if (pl_it != info.end() && pl_it->second.is_int()) piece_length = static_cast<uint64_t>(pl_it->second.as_int());

    // Pieces (concatenated SHA1 hashes)
    auto pieces_it = info.find("pieces");
    if (pieces_it != info.end() && pieces_it->second.is_string()) {
        const auto& pieces_str = pieces_it->second.as_string();
        size_t n = pieces_str.size() / 20;
        piece_hashes.resize(n);
        for (size_t i = 0; i < n; ++i) {
            std::memcpy(piece_hashes[i].data(), pieces_str.data() + i * 20, 20);
        }
    }

    // Files
    auto files_it = info.find("files");
    if (files_it != info.end() && files_it->second.is_list()) {
        // Multi-file torrent

        for (const auto& fval : files_it->second.as_list()) {
            if (!fval.is_dict()) continue;
            const auto& fdict = fval.as_dict();
            TorrentFile file;

            // Path (list of strings)
            auto path_it = fdict.find("path");
            if (path_it != fdict.end() && path_it->second.is_list()) {
                auto full_path = path_it->second.as_list()
                    | std::views::transform([](const BEncodeValue& b) { return b.as_string(); })
                    | std::views::join_with('/')
					| std::ranges::to<std::string>();

                file.path = std::move(full_path);
            }

            // Length
            auto len_it = fdict.find("length");
            if (len_it != fdict.end() && len_it->second.is_int()) {
                file.length = static_cast<uint64_t>(len_it->second.as_int());
                total_size += file.length;
            }

            files.push_back(std::move(file));
        }
    }
    else {
        // Single-file torrent
        auto len_it = info.find("length");
        if (len_it != info.end() && len_it->second.is_int()) {
            files.push_back({ std::string(name), static_cast<uint64_t>(len_it->second.as_int()) });
            total_size += len_it->second.as_int();
        }
    }

    // Optionals
    auto comment_it = dict.find("comment");
    if (comment_it != dict.end() && comment_it->second.is_string()) comment = comment_it->second.as_string();

    auto created_by_it = dict.find("created by");
    if (created_by_it != dict.end() && created_by_it->second.is_string()) created_by = created_by_it->second.as_string();

    auto creation_date_it = dict.find("creation date");
    if (creation_date_it != dict.end() && creation_date_it->second.is_int()) creation_date = static_cast<uint64_t>(creation_date_it->second.as_int());

    // Info hash (SHA1 of bencoded info dictionary)

	const auto& [start, end] = parser.get_info_start_end();

	std::string info_bencoded = in.substr(start, end - start);

    SHA1(reinterpret_cast<const unsigned char*>(info_bencoded.data()), info_bencoded.size(), info_hash.data());
}

void Metadata::compute_info_hash_hex() {
    static const char* hex = "0123456789abcdef";

    info_hash_hex.resize(40);

    for (size_t i = 0; i < 20; ++i) {
        unsigned char b = info_hash[i];
        info_hash_hex[2*i]     = hex[(b >> 4) & 0xF];
        info_hash_hex[2*i + 1] = hex[b & 0xF];
    }
}