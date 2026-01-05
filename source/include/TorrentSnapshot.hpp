#pragma once

#include <string>
#include <cstdint>

struct TorrentSnapshot {
    std::string name, hash;

    uint64_t total_size, downloaded, uploaded;

    double progress; // double down_rate, up_rate;

    uint64_t trackers, peers;

    std::string status; // downloading, seeding, stalled, paused
};
