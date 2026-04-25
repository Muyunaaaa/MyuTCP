#pragma once

// we realize a simple iss generator,
// however the real TCP ISS generator is more complex, it may consider the time, the process id and so on, here we just use a simple random number generator

#include <random>

namespace myu {
    inline uint32_t _generate_initial_seq() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

        return dis(gen);
    }
}
