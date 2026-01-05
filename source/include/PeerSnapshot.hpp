#pragma once

#include <string>
#include <cstdint>

struct PeerSnapshot {
    std::string ip{}, name{};

    double progress; // depending on peer bitfield

    bool choked = true, interested = false;

    int requests{};
};
