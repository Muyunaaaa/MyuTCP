#include "tcb.h"

#include <random>

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
}

bool myu::TcpSession::is_closed() const{
    return state_ == TcpState::CLOSED;
}

bool myu::TcpSession::is_connected() const{
    return state_ == TcpState::ESTABLISHED;
}

size_t myu::TcpSession::available() const{
    return recv_buffer_.size();
}

myu::TcpState myu::TcpSession::get_state() const{
    return state_;
}

std::pair<std::string, uint16_t> myu::TcpSession::get_local_addr() {

}
