#pragma once
#include <span>
#include <string>

#include "RingBuffer.h"
#include "sw.h"
#include "TimerManager.h"
#include "UdpDriver.h"

// TCP Control Block
namespace myu {
    enum TcpState {
        CLOSED,
        LISTEN,
        SYN_SENT,
        SYN_RECEIVED,
        ESTABLISHED,
        FIN_WAIT_1,
        FIN_WAIT_2,
        CLOSE_WAIT,
        CLOSING,
        LAST_ACK,
        TIME_WAIT
    };

    class TcpSession {
    private:
        TcpState state_ = TcpState::CLOSED;

        myu::send_window send_window_;
        myu::recv_window recv_window_;

        myu::RingQueue<uint8_t, 1024> send_buffer_;
        myu::RingQueue<uint8_t, 1024> recv_buffer_;
        std::map<uint32_t, myu::myu_tcp_packet> ooo_map_; // out-of-order packet map, key is the seq number

        TimerManager& timer_manager_;
        UdpDriver& udp_driver_;

        // !!! to be sure that all timers are stopped when the session is closed
        // otherwise the timer callback function may be called after the session is closed, which may cause undefined behavior
        void _transition_to(TcpState new_state);

        void _send_control_packet(uint8_t flags);
        void _send_pure_ack(uint32_t ack_num);
        void _send_payload_packet(const std::vector<uint8_t>& payload, uint8_t flags);

        // callbacks function
        std::function<void()> on_established_cb_;
        std::function<void(size_t)> on_data_cb_;
        std::function<void()> on_closed_cb_;
        std::function<void(const std::string&)> on_error_cb_;

        // utils

        // check seq, if the number is order, put the payload into recv_buffer_ and update recv_window_, then return true
        // otherwise put in ooo_map and return false
        bool _handle_incoming_packet(const myu::myu_tcp_packet& packet);

        // parse the ack_number in the header
        bool _handle_ack(const myu::myu_tcp_packet& packet);

        // as a callback function for timer, it will be called when the timer is timeout
        // then it will retransmit the packet and restart the timer
        // what this function will do?
        // 1. get the packet from send_buffer_ according to the seq number
        // 2. retransmit the packet using udp_driver_
        // 3. restart the timer using timer_manager_, may double the timeout for next time
        // if there too mant times to retransmit, we can consider the connection is broken and close it
        bool _handle_retransmit(const myu::myu_tcp_packet& packet);

        // when the state is ESTABLISHED and user use send function, we try to send the data in send_buffer_
        // if the sending window is not full, otherwise we just put the data in send_buffer_ and wait for the window to be available
        void _try_send();

    public:
        TcpSession(TimerManager& timer_manager, UdpDriver& udp_driver);

        // lifetime control
        void connect(const char* host, uint16_t port); // three handshake
        void close(); // four handshake
        bool is_connected();
        bool is_closed();

        // data transmission
        size_t send(std::span<const uint8_t> data);
        size_t recv(std::span<uint8_t> buf);
        size_t available(); // return the size of data that can be read from recv_buffer_

        // callbacks
        void set_on_established(std::function<void()> callback) {on_established_cb_ = std::move(callback);}
        void set_on_data(std::function<void(size_t)> callback) {on_data_cb_ = std::move(callback);}
        void set_on_closed(std::function<void()> callback) {on_closed_cb_ = std::move(callback);}
        void set_on_error(std::function<void(const std::string&)> callback) {on_error_cb_ = std::move(callback);}

        // state utils
        TcpState get_state();
        std::pair<std::string, uint16_t> get_local_addr();
        std::pair<std::string, uint16_t> get_remote_addr();
        uint32_t get_peer_window_size();
        uint32_t get_window_size();

        // handle packet
        void input(const myu::myu_tcp_packet& packet); // the entry function for handling incoming packet
        void handle_syn_sent(const myu::myu_tcp_packet& packet); // only handle SYN-ACK packet
        void handle_established(const myu::myu_tcp_packet& packet); // only handle ACK and data packet
        void handle_close_wait(const myu::myu_tcp_packet& packet); // only handle ACK and FIN packet
    };
}