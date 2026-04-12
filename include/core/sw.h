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
            if (send_next_ >= send_unack_) {
                return send_window_size_ - (send_next_ - send_unack_);
            } else {
                return send_unack_ - send_next_;
            }
        }
    };

    struct recv_window {
        uint32_t recv_next_; // the smallest seq number that has not been received
        uint32_t recv_window_size_; // the size of the receiving window
        uint32_t initial_recv_seq_; // the rand initial sequence number for receiving

        size_t get_usable_window_size() const {
            if (recv_next_ >= initial_recv_seq_) {
                return recv_window_size_ - (recv_next_ - initial_recv_seq_);
            } else {
                return initial_recv_seq_ - recv_next_;
            }
        }
    };
}
