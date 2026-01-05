#pragma once

#include <string>
#include <cstdint>

struct TrackerSnapshot {
    std::string url{};

    uint32_t interval{}, next_in{}, peers_returned{};

    std::string status{};

    bool reachable = false;
};
