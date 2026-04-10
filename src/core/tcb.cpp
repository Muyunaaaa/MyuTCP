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
    ooo_map_ = std::map<uint32_t, std::vector<uint8_t> >();

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
    _set_peer_usable_window_size(packet.header.window_size);
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
    uint32_t seq_num = packet.header.seq_num;
    _set_peer_usable_window_size(packet.header.window_size);
    // check if the packet is in order, it should be equal to recv_next_
    if (seq_num == recv_window_.recv_next_) {
        if (recv_buffer_.push_batch(packet.payload)) {
            recv_window_.recv_next_ += packet.payload.size();
        } else {
            spdlog::warn("Receive buffer is full, cannot push packet with seq {}, so it's discarded", seq_num);
        }
        spdlog::info("Received in-order packet with seq {}, updated recv_next to {}", seq_num, recv_window_.recv_next_);
        // check the ooo_map to see if there are some packets that can be put in order after this packet
        // put the payload in recv_buffer_
        for (auto it = ooo_map_.begin(); it != ooo_map_.end();) {
            if (it->first == recv_window_.recv_next_) {
                auto &ooo_packet_payload = it->second;
                if (recv_buffer_.push_batch(ooo_packet_payload)) {
                    recv_window_.recv_next_ += ooo_packet_payload.size();
                    it = ooo_map_.erase(it);
                    spdlog::info("Put out-of-order packet with seq {} in order, updated recv_next to {}", it->first,
                                 recv_window_.recv_next_);
                } else {
                    spdlog::warn("ooo_map buffer is full, cannot push packet with seq {}, so it's discarded", seq_num);
                }
            } else {
                break;
            }
        }
        // update recv_window_
        // send ack
        _send_pure_ack(recv_window_.recv_next_);
        spdlog::debug("After handling in-order packet, recv_next is {}, recv_buffer size is {}, ooo_map size is {}",
                      recv_window_.recv_next_, recv_buffer_.size(), ooo_map_.size());
        return true;
    } else {
        // put the packet in ooo_map
        uint32_t window_end = recv_window_.recv_next_ + recv_window_.recv_window_size_;
        if (seq_num > recv_window_.recv_next_ && seq_num < window_end) {
            ooo_map_[seq_num] = std::move(packet.payload);
        } else {
            spdlog::warn(
                "Received out-of-order packet with seq {}, but it's out of the receiving window, so it's discarded",
                seq_num);
        }
        // send ack
        // actually, we are supposed to mark next packet's ACK which has payload as 1, otherwise to send a pure ACK
        // simply, we just send a pure ACK
        // in the future, we can set a attribute to mark next packet if should be ACK1
        _send_pure_ack(recv_window_.recv_next_);
        return false;
    }
}

void myu::TcpSession::_send_pure_ack(uint32_t ack_num) {
    myu_tcp_packet packet;

    packet.header.s_port = htons(listener_port_);
    packet.header.d_port = htons(remote_port_);
    packet.header.seq_num = htonl(send_window_.send_next_);
    packet.header.ack_num = htonl(ack_num);

    packet.header.hl = 5;
    packet.header.ack = 1;

    packet.header.window_size = htons(static_cast<uint16_t>(get_usable_recv_window_size()));

    packet.payload.clear();

    _calculate_checksum(packet);

    sockaddr_in dest;
    uv_ip4_addr(get_remote_ip().c_str(), get_remote_port(), &dest);
    udp_driver_.send_packet(packet, dest);

    spdlog::debug("Sent Pure ACK: ack_num = {}, window = {}", ack_num, ntohs(packet.header.window_size));
}

// the flags parameter is a bitmask, which can be a combination of FLAG_SYN, FLAG_ACK, FLAG_FIN, FLAG_RST
// FIN - 0000 0001
// SYN - 0000 0010
// RST - 0000 0100
// PSH - 0000 1000
// ACK - 0001 0000
// URG - 0010 0000
// ECE - 0100 0000
// CWR - 1000 0000
void myu::TcpSession::_send_control_packet(uint8_t flags) {
    myu_tcp_packet packet;

    packet.header.s_port = htons(listener_port_);
    packet.header.d_port = htons(remote_port_);
    packet.header.seq_num = htonl(send_window_.send_next_);
    packet.header.ack_num = htonl(recv_window_.recv_next_);

    packet.header.hl = 5;
    packet.header.syn = (flags & FLAG_SYN) ? 1 : 0;
    packet.header.ack = (flags & FLAG_ACK) ? 1 : 0;
    packet.header.fin = (flags & FLAG_FIN) ? 1 : 0;
    packet.header.rst = (flags & FLAG_RST) ? 1 : 0;

    packet.header.window_size = htons(static_cast<uint16_t>(get_usable_recv_window_size()));

    sockaddr_in dest;
    uv_ip4_addr(get_remote_ip().c_str(), get_remote_port(), &dest);

    std::shared_ptr<myu_tcp_packet> packet_ptr = std::make_shared<myu_tcp_packet>(packet);
    if (packet.header.syn || packet.header.fin) {
        timer_manager_.start_timer(send_window_.send_next_, get_timeout_ms(), [this, packet_ptr]() {
            _handle_retransmit(packet_ptr, 0, ntohl(packet_ptr->header.seq_num), get_timeout_ms());
            spdlog::info("Retransmitted control packet with seq {}, flags {}", ntohl(packet_ptr->header.seq_num),
                         packet_ptr->header.syn ? "SYN" : "FIN");
        });
        send_window_.send_next_ += 1;
    }

    _calculate_checksum(*packet_ptr);
    udp_driver_.send_packet(*packet_ptr, dest);
}

void myu::TcpSession::_send_payload_packet(const std::vector<uint8_t> &payload, uint8_t flags) {
    myu_tcp_packet packet;

    packet.header.s_port = htons(listener_port_);
    packet.header.d_port = htons(remote_port_);
    packet.header.seq_num = htonl(send_window_.send_next_);
    packet.header.ack_num = htonl(recv_window_.recv_next_);

    packet.header.hl = 5;
    packet.header.ack = 1;
    if (flags & FLAG_PSH) packet.header.psh = 1;
    // if the PSH flag is set, it means that the data should be pushed to the application immediately

    packet.header.window_size = htons(static_cast<uint16_t>(get_usable_recv_window_size()));

    packet.payload = payload;
    uint32_t payload_len = static_cast<uint32_t>(payload.size());

    sockaddr_in dest;
    uv_ip4_addr(get_remote_ip().c_str(), get_remote_port(), &dest);

    std::shared_ptr<myu_tcp_packet> packet_ptr = std::make_shared<myu_tcp_packet>(packet);
    timer_manager_.start_timer(send_window_.send_next_, get_timeout_ms(), [this, packet_ptr]() {
        _handle_retransmit(packet_ptr, 0, ntohl(packet_ptr->header.seq_num), get_timeout_ms());
        spdlog::info("Retransmitted data packet with seq {}, len {}",
                         ntohl(packet_ptr->header.seq_num), packet_ptr->payload.size());
    });

    send_window_.send_next_ += payload_len;

    _calculate_checksum(*packet_ptr);
    udp_driver_.send_packet(*packet_ptr, dest);

    spdlog::info("Sent Data: seq = {}, len = {}", ntohl(packet.header.seq_num), payload_len);
}

bool myu::TcpSession::_handle_retransmit(std::shared_ptr<myu_tcp_packet> packet, uint32_t retransmit_count, uint32_t retr_seq_num, uint64_t next_timeout_ms) {
    packet->header.ack_num = htonl(recv_window_.recv_next_);
    packet->header.window_size = htons(static_cast<uint16_t>(get_usable_recv_window_size()));

    retransmit_count++;
    timer_manager_.stop_timer(retr_seq_num);
    if (retransmit_count >= MAX_RETRANSMIT_COUNT) {
        spdlog::error("Packet with seq {} has been retransmitted {} times, giving up", retr_seq_num, retransmit_count);
        //todo: we can consider the connection is broken and close it, or we can just give up this packet and move on, here we just give up this packet and move on
        return false;
    }
    uint64_t next_timeout_ = next_timeout_ms * (1 << retransmit_count);
    timer_manager_.start_timer(retr_seq_num, next_timeout_, [this, packet,retransmit_count]() {
    _handle_retransmit(packet, retransmit_count ,ntohl(packet->header.seq_num));
    spdlog::info("Retransmitted data packet with seq {}, len {}",
                     ntohl(packet->header.seq_num), packet->payload.size());
});

    _calculate_checksum(*packet);
    sockaddr_in dest_;
    uv_ip4_addr(get_remote_ip().c_str(), get_remote_port(), &dest_);
    udp_driver_.send_packet(*packet, dest_);

    return true;
}

void myu::TcpSession::_handle_try_send() {
}

void myu::TcpSession::_calculate_checksum(myu::myu_tcp_packet &packet) {
}
