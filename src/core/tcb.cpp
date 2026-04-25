#include "tcb.h"

#include <random>

#include "spdlog/spdlog.h"
#include "util/gen_iss.h"

myu::TcpSession::TcpSession(uv_loop_t *loop, UdpDriver *udp_driver) : timer_manager_(loop),
                                                                      udp_driver_(udp_driver),
                                                                      loop_(loop),
                                                                      listener_ip_("0.0.0.0"),
                                                                      remote_ip_("127.0.0.1") {
    state_ = TcpState::CLOSED;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 10000);
    send_window_ = {
        .send_unack_ = 0,
        .send_next_ = 0,
        .send_window_size_ = 1,
        .initial_send_seq_ = dis(gen)
    };
    recv_window_ = {
        .recv_next_ = 0,
        .recv_window_size_ = 1,
        .initial_recv_seq_ = dis(gen)
    };
    send_buffer_ = myu::RingQueue<uint8_t, 1024>();
    recv_buffer_ = myu::RingQueue<uint8_t, 1024>();
    ooo_map_ = std::map<uint32_t, std::vector<uint8_t> >();
    inflight_packets_ = std::map<uint32_t, myu_tcp_packet>();
    peer_usable_window_size_ = 0;
    last_ack_received_ = send_window_.initial_send_seq_;
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
        // if the user set the callback, it means that the user want to control when to close the connection.
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

void myu::TcpSession::set_on_recv_three_dup_ack(std::function<void(uint32_t ack_num)> callback) {
    if (callback) {
        on_recv_three_dup_ack = std::move(callback);
    } else {
        // if the user not set the callback, we provide a default callback function, which give a log when receive three duplicate ACKs
        on_recv_three_dup_ack = [this](uint32_t ack_num) {
            timer_manager_.stop_timer(ack_num);
            auto it = inflight_packets_.find(ack_num);
            if (it != inflight_packets_.end()) {
                const myu_tcp_packet &packet = it->second;
                // this function will start the timer automatically
                _send_payload_packet(packet.payload, packet.header.flags);
                spdlog::info("Received three duplicate ACKs for seq {}, fast retransmit packet with seq {}", ack_num,
                             ntohl(packet.header.seq_num));
            }
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
    spdlog::info("TCP State Transition: {} -> {}", state_to_string(state_), state_to_string(new_state));
    state_ = new_state;
}

bool myu::TcpSession::_handle_ack(const myu::myu_tcp_packet &packet) {
    uint32_t ack_num = ntohl(packet.header.ack_num);
    // ack value means that the seq_num would accept next time
    // check if the ack number is valid, it should be in the range of (send_unack_, send_next_]
    if (ack_num > send_window_.send_unack_ && ack_num <= send_window_.send_next_) {
        // drop timer for the acked packet
        // the squ_num in the packet persent the first byte of the bytes the peer want to receive
        timer_manager_.drop_timer_up_to(ack_num);

        // pop the acked data from send_buffer_
        uint32_t acked_bytes = ack_num - send_window_.send_unack_;
        send_buffer_.pop_front(acked_bytes);

        // update send_window_
        send_window_.send_unack_ = ack_num;

        // erase the all acked packets in inflight_packets_
        for (auto it = inflight_packets_.begin(); it != inflight_packets_.end();) {
            if (it->first < ack_num) {
                it = inflight_packets_.erase(it);
            } else {
                break;
            }
        }

        // if we receive a ack ahead of the ack we received last, we ignore it
        if (last_ack_received_ < ack_num) {
            last_ack_received_ = ack_num;
            dup_ack_count_ = 0;
        } else if (last_ack_received_ == ack_num) {
            dup_ack_count_++;
        } else {
            spdlog::warn("Received a ack ahead of the ack we received last, we ignore it");
        }

        // if we receive 3 duplicate ACKs, it means that the packet with seq number ack_num is lost,
        // we need to retransmit it immediately without waiting for the timer to timeout
        if (dup_ack_count_ == 3) {
            on_recv_three_dup_ack(ack_num);
            uint32_t swz = send_window_.send_window_size_;
            ssthresh_ = swz / 2;
            send_window_.send_window_size_ = swz / 2 + 3;
            spdlog::info("Received 3 dup ack, we adjust the send window size {} to {}", swz,
                         send_window_.send_window_size_);
            dup_ack_count_ = 0;
        }


        uint32_t swz = send_window_.send_window_size_;
        if (swz < ssthresh_) {
            send_window_.send_window_size_ = swz * 2;
            spdlog::info("After one rtt and the cwnd is under ssthresh = {} , we adjust the cwnd from {} to {}",
                        ssthresh_,
                         swz,
                        send_window_.send_window_size_
            );
        } else {
            send_window_.send_window_size_ = swz + 1;
            spdlog::info(
                "After one rtt and the cwnd is more than ssthresh = {}, we adjust the cwnd from {} to {}",
                ssthresh_,
                swz,
                send_window_.send_window_size_
            );
        }

        _handle_try_send();

        spdlog::info("Received ACK for seq {}, updated send_unack to {}", ack_num, send_window_.send_unack_);
        return true;
    }
    return false;
}

bool myu::TcpSession::_handle_incoming_packet(const myu::myu_tcp_packet &packet) {
    uint32_t seq_num = ntohl(packet.header.seq_num);
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
        // in the future, we can set a attribute to mark next packet whether should be ACK1
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

    packet.header.data_offset_and_reserved = (5 << 4);
    packet.header.flags = FLAG_ACK;

    packet.header.window_size = htons(static_cast<uint16_t>(get_usable_recv_window_size()));

    packet.payload.clear();
    std::shared_ptr<myu_tcp_packet> packet_ptr = std::make_shared<myu_tcp_packet>(packet);

    _calculate_checksum(*packet_ptr);

    sockaddr_in dest;
    uv_ip4_addr(get_remote_ip().c_str(), get_remote_port(), &dest);
    spdlog::info("Send Pure ACK to {}:{}", get_remote_ip(), get_remote_port());
    udp_driver_->send_packet(packet_ptr, dest);

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

    packet.header.data_offset_and_reserved = (5 << 4);
    packet.header.flags = flags;

    packet.header.window_size = htons(static_cast<uint16_t>(get_usable_recv_window_size()));

    std::string remote_ip = get_remote_ip();
    uint16_t remote_port = get_remote_port();

    sockaddr_in dest;
    uv_ip4_addr(remote_ip.c_str(), remote_port, &dest);
    std::shared_ptr<myu_tcp_packet> packet_ptr = std::make_shared<myu_tcp_packet>(packet);
    if (packet.header.flags & FLAG_SYN || packet.header.flags & FLAG_FIN) {
        timer_manager_.start_timer(send_window_.send_next_, get_timeout_ms(),
                                   [this, packet_ptr, remote_ip, remote_port]() {
                                       _handle_retransmit(packet_ptr, 0, ntohl(packet_ptr->header.seq_num),
                                                          get_timeout_ms(), remote_ip, remote_port);
                                       spdlog::info("Retransmitted control packet with seq {}, flags {}",
                                                    ntohl(packet_ptr->header.seq_num),
                                                    packet_ptr->header.flags & FLAG_SYN ? "SYN" : "FIN");
                                   });
        send_window_.send_next_ += 1;
    }

    _calculate_checksum(*packet_ptr);
    udp_driver_->send_packet(packet_ptr, dest);
}

void myu::TcpSession::_send_payload_packet(const std::vector<uint8_t> &payload, uint8_t flags) {
    if (payload.empty()) return;
    myu_tcp_packet packet;

    packet.header.s_port = htons(listener_port_);
    packet.header.d_port = htons(remote_port_);
    packet.header.seq_num = htonl(send_window_.send_next_);
    packet.header.ack_num = htonl(recv_window_.recv_next_);

    packet.header.data_offset_and_reserved = (5 << 4);
    packet.header.flags = flags;
    // if the PSH flag is set, it means that the data should be pushed to the application immediately

    packet.header.window_size = htons(static_cast<uint16_t>(get_usable_recv_window_size()));

    packet.payload = payload;
    uint32_t payload_len = static_cast<uint32_t>(payload.size());

    sockaddr_in dest;
    uv_ip4_addr(get_remote_ip().c_str(), get_remote_port(), &dest);

    inflight_packets_.try_emplace(packet.header.seq_num, packet);

    std::shared_ptr<myu_tcp_packet> packet_ptr = std::make_shared<myu_tcp_packet>(packet);
    timer_manager_.start_timer(send_window_.send_next_, get_timeout_ms(), [this, packet_ptr]() {
        _handle_retransmit(packet_ptr, 0, ntohl(packet_ptr->header.seq_num), get_timeout_ms());
        spdlog::info("Retransmitted data packet with seq {}, len {}",
                     ntohl(packet_ptr->header.seq_num), packet_ptr->payload.size());
    });

    send_window_.send_next_ += payload_len;

    _calculate_checksum(*packet_ptr);

    spdlog::info("Sent Data: seq = {}, len = {}, content = {}",
        ntohl(packet.header.seq_num),
        payload_len,
        std::string(packet_ptr->payload.begin(), packet_ptr->payload.end())
    );
    udp_driver_->send_packet(packet_ptr, dest);
}

bool myu::TcpSession::_handle_retransmit(std::shared_ptr<myu_tcp_packet> packet, uint32_t retransmit_count,
                                         uint32_t retr_seq_num, uint64_t next_timeout_ms, std::string captured_ip,
                                         uint16_t captured_port) {
    packet->header.ack_num = htonl(recv_window_.recv_next_);
    packet->header.window_size = htons(static_cast<uint16_t>(get_usable_recv_window_size()));

    retransmit_count++;
    timer_manager_.stop_timer(retr_seq_num);
    if (retransmit_count >= MAX_RETRANSMIT_COUNT) {
        spdlog::error("Packet with seq {} has been retransmitted {} times, giving up", retr_seq_num, retransmit_count);
        // we can consider the connection is broken and close it, or we can just give up this packet and move on, here we just give up this packet and move on
        return false;
    }

    uint32_t cwnd = send_window_.send_window_size_;
    uint32_t _ssthresh = ssthresh_;
    if (cwnd % 2) {
        ssthresh_ = cwnd / 2;
    }else {
        ssthresh_ = (cwnd + 1) / 2;
    }
    send_window_.send_window_size_ = 1;
    spdlog::info("Packet Lost! We reset the cwnd from {} to {}, ssthresh from {} to {}" ,
        cwnd,
        send_window_.send_window_size_,
        _ssthresh,
        ssthresh_
        );

    uint64_t next_timeout_ = next_timeout_ms * (1 << retransmit_count);
    spdlog::debug("this packet has been retransmit {} times, then the timeout_ms is {}", retransmit_count,
                  next_timeout_);
    timer_manager_.start_timer(retr_seq_num, next_timeout_,
                               [this, packet,retransmit_count, captured_ip, captured_port]() {
                                   _handle_retransmit(packet, retransmit_count, ntohl(packet->header.seq_num),
                                                      get_timeout_ms(), captured_ip, captured_port);
                                   spdlog::info("Retransmitted data packet with seq {}, len {}",
                                                ntohl(packet->header.seq_num), packet->payload.size());
                               });

    _calculate_checksum(*packet);
    sockaddr_in dest_;
    uv_ip4_addr(captured_ip.c_str(), captured_port, &dest_);
    udp_driver_->send_packet(packet, dest_);

    return true;
}

void myu::TcpSession::_handle_try_send() {
    if (state_ == TcpState::TIME_WAIT ||
        state_ == TcpState::FIN_WAIT_1 ||
        state_ == TcpState::FIN_WAIT_2 ||
        state_ == TcpState::CLOSED ||
        state_ == TcpState::LAST_ACK ||
        state_ == TcpState::LISTEN ||
        state_ == TcpState::SYN_RECEIVED ||
        state_ == TcpState::SYN_SENT
    )
        return;
    size_t usable_window_size = get_usable_send_window_size();
    size_t usable_peer_recv_window_size = get_peer_usable_recv_window_size();
    uint32_t effective_window_size = std::min(usable_window_size, usable_peer_recv_window_size);
    uint32_t inflight = send_window_.send_next_ - send_window_.send_unack_;
    uint32_t allowance = effective_window_size - inflight;

    while (allowance > 0 && !send_buffer_.empty()) {
        size_t mss = 1460;
        size_t unsent_in_buffer = send_buffer_.size() - inflight;
        size_t can_send = std::min({(size_t) allowance, (size_t) mss, unsent_in_buffer});

        if (can_send == 0) break;

        // peek size(= can_send) data from the data has not been sent
        std::vector<uint8_t> data = send_buffer_.peek_range(inflight, can_send);

        std::string debug_str(data.begin(), data.end());

        spdlog::info("Peek offset: {}, len: {}, content: '{}'",
                     inflight, data.size(), debug_str);

        _send_payload_packet(data, FLAG_ACK);

        allowance -= data.size();
        inflight += data.size();
    }
}

void myu::TcpSession::_calculate_checksum(myu::myu_tcp_packet &packet) {
    // according to RFC1071,set checksum field to 0 before calculating checksum
    packet.header.checksum = 0;
    // even though the checksum field is 16 bits, we use a 32-bit integer to store the sum to avoid overflow
    uint32_t sum = 0;

    // the core logic
    auto add_to_sum = [&sum](const void *data, size_t len) {
        const uint16_t *ptr = static_cast<const uint16_t *>(data);
        while (len > 1) {
            sum += *ptr++;
            len -= 2;
        }
        if (len > 0) {
            sum += *reinterpret_cast<const uint8_t *>(ptr);
        }
    };

    add_to_sum(&packet.header, sizeof(packet.header));

    if (!packet.payload.empty()) {
        add_to_sum(packet.payload.data(), packet.payload.size());
    }

    // fold the sum to 16 bits and take the one's complement
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    packet.header.checksum = static_cast<uint16_t>(~sum);
}

bool myu::TcpSession::_verify_checksum(const myu::myu_tcp_packet &packet) {
    uint32_t sum = 0;

    auto add_to_sum = [&](const void *data, size_t len) {
        const uint16_t *ptr = static_cast<const uint16_t *>(data);
        while (len > 1) {
            sum += *ptr++;
            len -= 2;
        }
        if (len > 0) {
            sum += *(static_cast<const uint8_t *>(static_cast<const void *>(ptr)));
        }
    };

    add_to_sum(&packet.header, sizeof(packet.header));

    if (!packet.payload.empty()) {
        add_to_sum(packet.payload.data(), packet.payload.size());
    }


    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    if (static_cast<uint16_t>(~sum) == 0) {
        return true;
    } else {
        spdlog::warn(
            "Checksum verification failed for packet with seq {}, expected checksum = {}, calculated checksum = {}",
            ntohl(packet.header.seq_num), ntohs(packet.header.checksum), static_cast<uint16_t>(~sum));
        return false;
    }
}

size_t myu::TcpSession::send(std::span<const uint8_t> data) {
    if (data.empty()) { return 0; }

    size_t buffer_free_space = send_buffer_.capacity() - send_buffer_.size();
    size_t to_write = std::min(data.size(), buffer_free_space);

    if (buffer_free_space < data.size()) {
        spdlog::warn("buffer free space is too small, only {} bytes can be written, but the data size is {}",
                     buffer_free_space, data.size());
    }

    bool success = send_buffer_.push_batch(data.subspan(0, to_write));

    if (!success) { return 0; }

    _handle_try_send();

    return to_write;
}

size_t myu::TcpSession::recv(std::span<uint8_t> buf) {
    size_t to_read = 0;
    if (!recv_buffer_.empty() && !buf.empty()) {
        to_read = std::min(buf.size(), recv_buffer_.size());
        for (size_t i = 0; i < to_read; ++i) {
            buf[i] = recv_buffer_.front();
            recv_buffer_.pop_front(1);
        }
    }

    if (recv_buffer_.empty() && state_ == TcpState::CLOSE_WAIT) {
        if (auto_close_on_eof) {
            spdlog::info("Buffer is empty and we are in CLOSE_WAIT. Closing session.");
            this->close();
        }
    }

    if (to_read > 0) return to_read;

    if (state_ == TcpState::CLOSE_WAIT || state_ == TcpState::LAST_ACK || state_ == CLOSED) {
        return 0;
    }

    return 0;
}

std::vector<uint8_t> myu::TcpSession::read_all() {
    size_t to_read = recv_buffer_.size();
    std::vector<uint8_t> data(to_read);
    for (size_t i = 0; i < to_read; ++i) {
        data[i] = recv_buffer_.front();
        recv_buffer_.pop_front(1);
    }

    if (recv_buffer_.empty() && state_ == TcpState::CLOSE_WAIT) {
        if (auto_close_on_eof) {
            spdlog::info("Buffer is empty and we are in CLOSE_WAIT. Closing session.");
            this->close();
        }
    }

    return data;
}

void myu::TcpSession::input(const myu::myu_tcp_packet &packet) {
    // verify the checksum
    if (!_verify_checksum(packet)) {
        spdlog::warn("the checksum of packet seq = {} is wrong");
        return;
    }
    // parse the header
    // we may do some special operations for some special flags
    // if the rst is 1, we need to release all resources and transition to CLOSED state directly, no matter what the current state is
    if (packet.header.flags & FLAG_RST) {
        spdlog::warn("Received RST packet, transition to CLOSED state directly");
        handle_reset();
        return;
    }
    // only the first syn packet's ack is 0, when we receive a non-syn packet and it ack is 0, we need to warn.
    if ((!(packet.header.flags & FLAG_ACK) && !(packet.header.flags & FLAG_SYN))
        && (!(packet.header.flags & FLAG_ACK) && !(packet.header.flags & FLAG_FIN))
    ) {
        spdlog::warn("Received non-SYN packet with ACK=0, which is unexpected");
        return;
    }
    // update the peer usable window size according to the window size field in the header of the packet, which is the size of bytes the peer can accept
    _set_peer_usable_window_size(ntohs(packet.header.window_size));
    // according to now state, call the corresponding handler function to handle the packet
    switch (state_) {
        case TcpState::CLOSED:
            user_want_to_close_ = false;
            break;
        case TcpState::LISTEN:
            handle_listen(packet);
            break;
        case TcpState::SYN_SENT:
            handle_syn_sent(packet);
            break;
        case TcpState::SYN_RECEIVED:
            handle_syn_received(packet);
            break;
        case TcpState::ESTABLISHED:
            handle_established(packet);
            break;
        case TcpState::FIN_WAIT_1:
            handle_fin_wait_1(packet);
            break;
        case TcpState::FIN_WAIT_2:
            handle_fin_wait_2(packet);
            break;
        case TcpState::CLOSE_WAIT:
            handle_close_wait(packet);
            break;
        case TcpState::LAST_ACK:
            handle_last_ack(packet);
            break;
        case TcpState::TIME_WAIT:
            handle_timed_wait(packet);
            break;
        default:
            spdlog::warn("Received packet in unexpected state {}, ignore it.", state_to_string(get_state()));
            break;
    }
}

void myu::TcpSession::connect() {
    spdlog::info("Start to connect to {}:{}", get_remote_ip(), get_remote_port());

    _transition_to(TcpState::SYN_SENT);

    udp_driver_->set_on_receive([&](const myu::myu_tcp_packet &packet, const sockaddr_in &addr) {
        spdlog::info("Received packet from {}:{}, flag {}, content = {}",
            inet_ntoa(addr.sin_addr),
            ntohs(addr.sin_port),
            parse_flags_to_string(packet.header.flags),
            std::string(packet.payload.begin(), packet.payload.end())
            );
        this->input(packet);
    });

    // initialize the send_window with a random number
    uint32_t iss = _generate_initial_seq();
    send_window_.send_next_ = iss;
    send_window_.send_unack_ = iss;

    time_collect_rtt_start_ = std::chrono::steady_clock::now();

    spdlog::info("Send a SYN packet to {}:{}", get_remote_ip(), get_remote_port());
    _send_control_packet(FLAG_SYN);

    send_window_.send_next_ = iss + 1;
}

void myu::TcpSession::close() {
    if (get_state() == ESTABLISHED) {
        _handle_try_send();
        if (!send_buffer_.empty()) {
            spdlog::warn("There are still {} bytes data in send buffer, but user want to close the connection, we will discard these data and close the connection",
                         send_buffer_.size());
        }
        timer_manager_.stop_all_timers();
        _transition_to(TcpState::FIN_WAIT_1);
        spdlog::info("Send a FIN packet to {}:{}", get_remote_ip(), get_remote_port());
        _send_control_packet(FLAG_FIN);
        user_want_to_close_ = true;
    } else if (get_state() == CLOSE_WAIT) {
        _transition_to(TcpState::LAST_ACK);
        _send_control_packet(FLAG_FIN | FLAG_ACK);
        user_want_to_close_ = true;
    } else if (get_state() == SYN_SENT) {
        // if we are in SYN_SENT state, it means that we are trying to connect to the peer,
        // but we want to give up this connection, so we just transition to CLOSED state directly
        spdlog::warn("Try to close the connection in SYN_SENT state, transition to CLOSED state directly");
        _send_control_packet(FLAG_RST);
        this->timer_manager_.stop_all_timers();
        _transition_to(TcpState::CLOSED);
    } else if (get_state() == SYN_RECEIVED) {
        // if we are in SYN_RECEIVED state, it means that we have received a SYN packet from the peer, but we have not received the ACK packet from the peer,
        // but we want to give up this connection, so we just transition to CLOSED state directly
        _send_control_packet(FLAG_RST);
        this->timer_manager_.stop_all_timers();
        _transition_to(TcpState::CLOSED);
    } else {
        spdlog::warn("Received close request in unexpected state {}, ignore it.", state_to_string(get_state()));
    }
}

void myu::TcpSession::listen() {
    if (get_state() != CLOSED) {
        spdlog::warn("Received listen packet in unexpected state {}, ignore it.", state_to_string(get_state()));
        return;
    }

    _transition_to(TcpState::LISTEN);
}


void myu::TcpSession::handle_syn_sent(const myu::myu_tcp_packet &packet) {
    uint32_t ack_num = ntohl(packet.header.ack_num);
    uint32_t seq_num = ntohl(packet.header.seq_num);
    uint32_t expected_ack = send_window_.send_unack_ + 1;
    if (packet.header.flags & FLAG_SYN && packet.header.flags & FLAG_ACK) {
        if (ack_num != expected_ack) {
            spdlog::warn("Received SYN-ACK packet with unexpected ack_num {}, expected ack_num is {}",
                         ack_num, expected_ack);
            return;
        }


        recv_window_.recv_next_ = seq_num + 1;
        send_window_.send_unack_ = ack_num;
        timer_manager_.stop_timer(expected_ack - 1);
        _send_pure_ack(recv_window_.recv_next_);
        _transition_to(ESTABLISHED);
        if (on_established_cb_) {
            on_established_cb_();
        }
        _handle_try_send();
    } else if (packet.header.flags & FLAG_SYN) {
        // it means that the peer also send a SYN packet to us
        // we should reply with a SYN-ACK packet, and transition to SYN_RECEIVED state
        recv_window_.recv_next_ = seq_num + 1;
        _transition_to(SYN_RECEIVED);
        spdlog::info("Send a SYN-ACK packet to {}:{}", get_remote_ip(), get_remote_port());
        _send_control_packet(FLAG_SYN | FLAG_ACK);
    }
}

void myu::TcpSession::handle_fin_wait_1(const myu::myu_tcp_packet &packet) {
    uint32_t incoming_ack = ntohl(packet.header.ack_num);
    uint32_t peer_seq = ntohl(packet.header.seq_num);
    uint32_t expected_ack = send_window_.send_unack_ + 1;

    if (packet.header.flags & FLAG_ACK && incoming_ack == expected_ack) {
        _transition_to(TcpState::FIN_WAIT_2);
        send_window_.send_unack_ = incoming_ack;
        timer_manager_.stop_timer(incoming_ack - 1);
    }
    if (packet.header.flags & FLAG_FIN) {
        recv_window_.recv_next_ = peer_seq + 1;
        spdlog::info("Send a pure ACK packet to {}:{}", get_remote_ip(), get_remote_port());
        _send_pure_ack(recv_window_.recv_next_);
        _transition_to(TcpState::CLOSE_WAIT);
    }
}

void myu::TcpSession::handle_fin_wait_2(const myu::myu_tcp_packet &packet) {
    if (packet.header.flags & FLAG_FIN) {
        uint32_t peer_seq = ntohl(packet.header.seq_num);
        recv_window_.recv_next_ = peer_seq + 1;
        spdlog::info("Send a pure ACK packet to {}:{}", get_remote_ip(), get_remote_port());
        _send_pure_ack(recv_window_.recv_next_);
        _transition_to(TcpState::TIME_WAIT);
        this->_start_2msl_timer();
    }
}

void myu::TcpSession::handle_timed_wait(const myu::myu_tcp_packet &packet) {
    // if we receive a duplicate FIN packet, we should resend the ACK packet
    if (packet.header.flags & FLAG_FIN) {
        uint32_t peer_seq = ntohl(packet.header.seq_num);
        recv_window_.recv_next_ = peer_seq + 1;
        spdlog::info("Received duplicate FIN packet, resend a pure ACK packet to {}:{}", get_remote_ip(),
                     get_remote_port());
        _send_pure_ack(recv_window_.recv_next_);
    }
}

void myu::TcpSession::handle_listen(const myu::myu_tcp_packet &packet) {
    if (!(packet.header.flags & FLAG_SYN)) return;

    uint32_t client_isn = ntohl(packet.header.seq_num);

    // initialize the send_window with a random number
    uint32_t iss = _generate_initial_seq();
    send_window_.send_next_ = iss;
    send_window_.send_unack_ = iss;

    recv_window_.recv_next_ = client_isn + 1;

    time_collect_rtt_start_ = std::chrono::steady_clock::now();

    // bind the remote addr
    set_remote_addr(get_remote_ip().c_str(), get_remote_port());
    _transition_to(TcpState::SYN_RECEIVED);

    spdlog::info("Send a SYN-ACK packet to {}:{}", get_remote_ip(), get_remote_port());
    _send_control_packet(FLAG_SYN | FLAG_ACK);

    send_window_.send_next_ = iss + 1;
}

void myu::TcpSession::handle_syn_received(const myu::myu_tcp_packet &packet) {
    if (!(packet.header.flags & FLAG_ACK)) return;

    uint32_t incoming_ack = ntohl(packet.header.ack_num);
    uint32_t expected_ack = send_window_.send_unack_ + 1;

    if (incoming_ack != expected_ack) return;

    send_window_.send_unack_ = incoming_ack;
    timer_manager_.stop_timer(incoming_ack - 1);

    time_collect_rtt_end_ = std::chrono::steady_clock::now();

    _transition_to(TcpState::ESTABLISHED);
    if (on_established_cb_) { on_established_cb_(); }
    _handle_try_send();
}

void myu::TcpSession::handle_close_wait(const myu::myu_tcp_packet &packet) {
    spdlog::info("Send a pure ACK packet to {}:{}", get_remote_ip(), get_remote_port());
    _send_pure_ack(recv_window_.recv_next_);
    _handle_try_send();
    if (!user_want_to_close_) {
        spdlog::warn("User don't want to close the connection");
    }
    if (!send_buffer_.empty()) {
        spdlog::warn("There are still data in the send buffer, so the connection cannot be closed immediately");
    }
    if (send_window_.send_unack_ != send_window_.send_next_) {
        spdlog::warn("The data in flight has not been acknowledged, so the connection cannot be closed immediately");
    }
    if (user_want_to_close_ && send_buffer_.empty() && send_window_.send_unack_ == send_window_.send_next_) {
        timer_manager_.stop_all_timers();
        spdlog::info("Send a FIN packet to {}:{}", get_remote_ip(), get_remote_port());
        _send_control_packet(FLAG_FIN);
        send_window_.send_next_++;
        _transition_to(TcpState::LAST_ACK);
    }
}

void myu::TcpSession::handle_last_ack(const myu::myu_tcp_packet &packet) {
    if (!(packet.header.flags & FLAG_ACK)) return;

    uint32_t incoming_ack = ntohl(packet.header.ack_num);
    uint32_t expected_ack = send_window_.send_unack_ + 1;

    if (incoming_ack != expected_ack) return;

    send_window_.send_unack_ = incoming_ack;
    timer_manager_.stop_timer(incoming_ack - 1);

    _transition_to(TcpState::CLOSED);
    if (on_closed_cb_) { on_closed_cb_(); }
}

void myu::TcpSession::handle_reset() {
    timer_manager_.stop_all_timers();
    _transition_to(TcpState::CLOSED);
    if (on_closed_cb_) { on_closed_cb_(); }
    // if (on_error_cb_) {
    //     on_error_cb_("Connection reset by peer");
    // }
}

void myu::TcpSession::handle_established(const myu::myu_tcp_packet &packet) {
    size_t payload_len = packet.payload.size();
    uint32_t peer_seq = ntohl(packet.header.seq_num);

    if (packet.header.flags & FLAG_ACK) {
        _handle_ack(packet);
        _handle_try_send();
    }

    if (payload_len > 0) {
        bool is_order = _handle_incoming_packet(packet);
        if (is_order && on_data_cb_) { on_data_cb_(available()); }
    }

    if (packet.header.flags & FLAG_FIN) {
        recv_window_.recv_next_ = peer_seq + 1;
        spdlog::info("Send a pure ACK packet to {}:{}", get_remote_ip(), get_remote_port());
        _send_pure_ack(recv_window_.recv_next_);
        _transition_to(TcpState::CLOSE_WAIT);
        if (on_data_cb_) { on_data_cb_(0); }
        // notify the application that the connection is closed by peer, and there is no more data to read
    }
}

void myu::TcpSession::_start_2msl_timer() {
    // const uint64_t TWO_MSL_TIMEOUT_MS = 2 * 60 * 1000; // 2 minutes in milliseconds
    constexpr uint64_t TWO_MSL_TIMEOUT_MS = 2 * 1000; // TODO: set it to 2 minutes in the future
    if (!timer_manager_._2msl_timer) {
        timer_manager_._2msl_timer = std::make_unique<uv_timer_t>();
        uv_timer_init(timer_manager_.loop_, timer_manager_._2msl_timer.get());
    }

    timer_manager_._2msl_timer->data = this;

    uv_timer_start(timer_manager_._2msl_timer.get(), [](uv_timer_t *handle) {
        auto *session = static_cast<TcpSession *>(handle->data);
        session->_handle_2msl_timeout();
    }, TWO_MSL_TIMEOUT_MS, 0);
}

void myu::TcpSession::_handle_2msl_timeout() {
    _transition_to(TcpState::CLOSED);
    uv_timer_stop(timer_manager_._2msl_timer.get());
    if (on_closed_cb_) {
        on_closed_cb_();
    }
}
