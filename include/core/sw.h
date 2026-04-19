#pragma once

#include <map>

#include "uv.h"

// SlidingWindow
namespace myu {
    struct send_window {
        uint32_t send_unack_; // the smallest seq number that has not been acknowledged
        uint32_t send_next_;  // the smallest seq number that has not been sent
        uint32_t send_window_size_; // the size of the sending window
        uint32_t initial_send_seq_; // the rand initial sequence number for sending

        size_t get_usable_window_size() const {
            uint32_t inflight = send_next_ - send_unack_;

            if (send_window_size_ > inflight) {
                return send_window_size_ - inflight;
            }

            return 0;
        }
    };

    struct recv_window {
        uint32_t recv_next_; // the smallest seq number that has not been received
        uint32_t recv_window_size_; // the size of the receiving window
        uint32_t initial_recv_seq_; // the rand initial sequence number for receiving

    };
}
