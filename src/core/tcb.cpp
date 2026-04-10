#include "tcb.h"

#include <random>

#include "spdlog/spdlog.h"

myu::TcpSession::TcpSession(TimerManager &timer_manager, UdpDriver &udp_driver)
    : timer_manager_(timer_manager), udp_driver_(udp_driver) {
    state_ = TcpState::CLOSED;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 10000);
    send_window_ = {
        .send_unack_ = 0,
        .send_next_ = 0,
        .send_window_size_ = 1024,
        .initial_send_seq_ = dis(gen)
    };
    recv_window_ = {
        .recv_next_ = 0,
        .recv_window_size_ = 1024,
        .initial_recv_seq_ = dis(gen)
    };
    send_buffer_ = myu::RingQueue<uint8_t, 1024>();
    recv_buffer_ = myu::RingQueue<uint8_t, 1024>();
    ooo_map_ = std::map<uint32_t, myu::myu_tcp_packet>();

    // default listener and remote address, it can be changed by the user
    listener_ip_ = "0.0.0.0";
    listener_port_ = 9999;
    remote_ip_ = "127.0.0.1";
    remote_port_ = 9999;
}


void myu::TcpSession::set_on_established(std::function<void()> callback) {
    if (callback) {
        on_established_cb_ = std::move(callback);
    } else {
        // if the user not set the callback, we provide a default callback function, which give a log when the connection is established
        on_established_cb_ = []() {
            spdlog::info("Connection established");
        };
    }
}

void myu::TcpSession::set_on_data(std::function<void(size_t)> callback) {
    if (callback) {
        on_data_cb_ = std::move(callback);
    } else {
        // if the user not set the callback, we provide a default callback function, which give a log when receive data
        on_data_cb_ = [](size_t size) {
            spdlog::info("Received data of size {}", size);
        };
    }
}

void myu::TcpSession::set_on_closed(std::function<void()> callback) {
    if (callback) {
        on_closed_cb_ = std::move(callback);
    } else {
        // if the user not set the callback, we provide a default callback function, which give a log when the connection is closed
        on_closed_cb_ = []() {
            spdlog::info("Connection closed");
        };
    }
}

void myu::TcpSession::set_on_error(std::function<void(const std::string &)> callback) {
    if (callback) {
        on_error_cb_ = std::move(callback);
    } else {
        // if the user not set the callback, we provide a default callback function, which give a log when an error occurs
        on_error_cb_ = [](const std::string &error) {
            spdlog::error("Error: {}", error);
        };
    }
}

bool myu::TcpSession::is_closed() const {
    return state_ == TcpState::CLOSED;
}

bool myu::TcpSession::is_connected() const {
    return state_ == TcpState::ESTABLISHED;
}

size_t myu::TcpSession::available() const {
    return recv_buffer_.size();
}

myu::TcpState myu::TcpSession::get_state() const {
    return state_;
}

std::pair<std::string, uint16_t> myu::TcpSession::get_listen_addr() const {
    return std::pair<std::string, uint16_t>(listener_ip_, listener_port_);
}

std::pair<std::string, uint16_t> myu::TcpSession::get_remote_addr() const {
    return std::pair<std::string, uint16_t>(remote_ip_, remote_port_);
}

void myu::TcpSession::_transition_to(TcpState new_state) {
    spdlog::info("Transitioning from {} to {}", state_to_string(state_), state_to_string(new_state));
    state_ = new_state;
}

bool myu::TcpSession::_handle_ack(const myu::myu_tcp_packet &packet) {
    uint32_t ack_num = packet.header.ack_num;
    // ack value means that the seq_num would accept next time
    // check if the ack number is valid, it should be in the range of (send_unack_, send_next_]
    if (ack_num > send_window_.send_unack_ && ack_num <= send_window_.send_next_) {
        // stop timer for the acked packet
        // the squ_num in the packet persent the first byte of the bytes the peer want to receive
        timer_manager_.stop_timers_up_to(ack_num);

        // pop the acked data from send_buffer_
        uint32_t acked_bytes = ack_num - send_window_.send_unack_;
        send_buffer_.pop_front(acked_bytes);

        // update send_window_
        send_window_.send_unack_ = ack_num;

        // todo: we need to adjust the strategy for updating the window size, here we just set it as the size of the peer window can accept
        send_window_.send_window_size_ = packet.header.window_size;

        spdlog::info("Received ACK for seq {}, updated send_unack to {}", ack_num, send_window_.send_unack_);
        return true;
    } else {
        spdlog::warn("Received invalid ACK for seq {}, current send_unack is {}", ack_num, send_window_.send_unack_);
        return false;
    }
}

bool myu::TcpSession::_handle_incoming_packet(const myu::myu_tcp_packet &packet) {

}

void myu::TcpSession::_send_pure_ack(uint32_t ack_num) {

}

void myu::TcpSession::_send_control_packet(uint8_t flags) {

}

void myu::TcpSession::_send_payload_packet(const std::vector<uint8_t> &payload, uint8_t flags) {

}

bool myu::TcpSession::_handle_retransmit(const myu::myu_tcp_packet &packet) {

}

void myu::TcpSession::_handle_try_send() {

}
