#pragma once
#include <memory>
#include <span>
#include <string>

#include "RingBuffer.h"
#include "sw.h"
#include "TimerManager.h"
#include "UdpDriver.h"


// TCP Control Block
namespace myu {
    constexpr uint32_t INITIAL_RTO = 1000;
    constexpr uint32_t RTO_MIN = 200;
    constexpr uint32_t RTO_MAX = 60000;
    constexpr uint32_t INITIAL_SSTHRESH = 8;

    enum TcpState {
        CLOSED,
        LISTEN,
        SYN_SENT,
        SYN_RECEIVED,
        ESTABLISHED,
        FIN_WAIT_1,
        FIN_WAIT_2,
        CLOSE_WAIT,
        LAST_ACK,
        TIME_WAIT
    };

    inline std::string state_to_string(TcpState state) {
        switch (state) {
            case TcpState::CLOSED: return "CLOSED";
            case TcpState::LISTEN: return "LISTEN";
            case TcpState::SYN_SENT: return "SYN_SENT";
            case TcpState::SYN_RECEIVED: return "SYN_RECEIVED";
            case TcpState::ESTABLISHED: return "ESTABLISHED";
            case TcpState::FIN_WAIT_1: return "FIN_WAIT_1";
            case TcpState::FIN_WAIT_2: return "FIN_WAIT_2";
            case TcpState::CLOSE_WAIT: return "CLOSE_WAIT";
            case TcpState::LAST_ACK: return "LAST_ACK";
            case TcpState::TIME_WAIT: return "TIME_WAIT";
            default: return "UNKNOWN_STATE";
        }
    }

    enum TcpFlag : uint8_t {
        // TcpFlag : uint8_t code
        FLAG_FIN = 0x01, // 0000 0001
        FLAG_SYN = 0x02, // 0000 0010
        FLAG_RST = 0x04, // 0000 0100
        FLAG_PSH = 0x08, // 0000 1000
        FLAG_ACK = 0x10, // 0001 0000
        FLAG_URG = 0x20, // 0010 0000
        FLAG_ECE = 0x40, // 0100 0000
        FLAG_CWR = 0x80 // 1000 0000
    };

    inline std::string to_string(TcpFlag flag) {
        switch (flag) {
            case FLAG_FIN: return "FIN";
            case FLAG_SYN: return "SYN";
            case FLAG_RST: return "RST";
            case FLAG_PSH: return "PSH";
            case FLAG_ACK: return "ACK";
            case FLAG_URG: return "URG";
            case FLAG_ECE: return "ECE";
            case FLAG_CWR: return "CWR";
            default: return "UNKNOWN_FLAG";
        }
    }

    inline std::string parse_flags_to_string(uint8_t flags) {
        switch (flags) {
            case 0x01: return "FLAG_FIN";
            case 0x02: return "FLAG_SYN";
            case 0x04: return "FLAG_RST";
            case 0x08: return "FLAG_PSH";
            case 0x10: return "FLAG_ACK";
            case 0x20: return "FLAG_URG";
            case 0x40: return "FLAG_ECE";
            case 0x80: return "FLAG_CWR";
            case 0x11: return "FLAG_FIN_ACK";
            case 0x12: return "FLAG_SYN_ACK";
            default: throw std::invalid_argument("Invalid TCP flag value");
        }
    }

    class TcpSession {
        using NotifyStackCb = std::function<void(TcpSession *)>;

    private:
        TcpState state_ = TcpState::CLOSED;

        myu::send_window send_window_;
        myu::recv_window recv_window_;

        // when we receive the first callback, we calculate the time cost to initialize the rtt
        bool is_rrt_initialized_ = false;
        uint32_t rtt_ = INITIAL_RTO / 2;

        uint32_t ssthresh_ = 8;
        uint32_t current_rto_ = INITIAL_RTO;
        uint32_t last_ack_received_;
        int dup_ack_count_ = 0; // to record the times of receiving the same ack

        myu::RingQueue<uint8_t, 1024> send_buffer_;
        myu::RingQueue<uint8_t, 1024> recv_buffer_;
        std::map<uint32_t, std::vector<uint8_t> > ooo_map_; // out-of-order packet map, key is the seq number
        std::map<uint32_t, myu_tcp_packet> inflight_packets_;
        // store the inflight packets and release them if receive their ack

        TimerManager timer_manager_;
        UdpDriver *udp_driver_;

        std::string listener_ip_;
        uint16_t listener_port_;
        std::string remote_ip_;
        uint16_t remote_port_;

        uv_loop_t *loop_ = nullptr;

        bool user_want_to_close_ = false;
        // when the user call close function, we set this flag to true, and we will try to close the connection when the send buffer is empty

        uint32_t peer_usable_window_size_;
        // save the usable window size of peer, which is updated when receive packet, and used to calculate the usable window size for sending
        uint64_t timeout_ms_ = 2000;
        // default timeout for retransmission, it can be adjusted according to the network condition

        // !!! to be sure that all timers are stopped when the session is closed
        // otherwise the timer callback function may be called after the session is closed, which may cause undefined behavior
        void _transition_to(TcpState new_state);

        void _send_control_packet(uint8_t flags);

        void _send_pure_ack(uint32_t ack_num);

        void _send_payload_packet(const std::vector<uint8_t> &payload, uint8_t flags);

        void _start_2msl_timer();

        // callbacks function
        std::function<void()> on_established_cb_;
        // if the user set the callback, we will call it when the connection is established
        std::function<void(size_t)> on_data_cb_;
        // if the user set the callback, we will call it when receive data, and pass the size of data in the recv_buffer_
        std::function<void()> on_closed_cb_;
        // if the user set the callback, we will call it when the connection is closed
        std::function<void(const std::string &)> on_error_cb_;
        // if the user set the callback, we will call it when an error occurs, and pass the error message
        std::function<void(uint32_t)> on_recv_three_dup_ack;

        // utils

        // check seq, if the number is order, put the payload into recv_buffer_ and update recv_window_, then return true
        // otherwise put in ooo_map and return false
        bool _handle_incoming_packet(const myu::myu_tcp_packet &packet);

        // parse the ack_number in the header, check the ack number is valid, if valid, update send_window_ and stop the timer, then return true
        bool _handle_ack(const myu::myu_tcp_packet &packet);

        // as a callback function for timer, it will be called when the timer is timeout
        // then it will retransmit the packet and restart the timer
        // what will this function do?
        // 1. get the packet from send_buffer_ according to the seq number
        // 2. retransmit the packet using udp_driver_
        // 3. restart the timer using timer_manager_, may double the timeout for next time
        // if there too mant times to retransmit, we can consider the connection is broken and close it
        bool _handle_retransmit(std::shared_ptr<myu_tcp_packet> packet, uint32_t retransmit_count = 0,
                                uint32_t retr_seq_num = 0, uint64_t next_timeout_ms = 2000,
                                std::string captured_ip = "127.0.0.1", uint16_t captured_port = 9999);

        // when the state is ESTABLISHED and user use send function, we try to send the data in send_buffer_
        // if the sending window is not full, otherwise we just put the data in send_buffer_ and wait for the window to be available
        void _handle_try_send();

        void _handle_2msl_timeout();

        void _set_peer_usable_window_size(uint32_t size) {
            peer_usable_window_size_ = size;
        }

        void _set_timeout_ms(uint32_t timeout_ms) {
            timeout_ms_ = timeout_ms;
        }

        // other
        void _calculate_checksum(myu::myu_tcp_packet &packet);

        bool _verify_checksum(const myu::myu_tcp_packet &packet);

        // the function which notifies the stack that the session is ready to be processed.
        NotifyStackCb notify_stack_cb_;
        // mark the session whether already is in the ready queue.
        bool is_in_ready_queue_ = false;

    public:
        // when the server is at CLOSE_WAIT and the buffer is empty, if this flag is true, we will close the connection immediately
        bool auto_close_on_eof = true;

        void set_in_ready_queue(bool in) { is_in_ready_queue_ = in; }

        // when the session is ready to be processed, we call this function to notify the stack, and the stack will put this session into the ready queue.
        void _trigger_event() {
            if (!is_in_ready_queue_ && notify_stack_cb_) {
                is_in_ready_queue_ = true;
                notify_stack_cb_(this);
            }
        }

        void set_notify_cb(NotifyStackCb cb) { notify_stack_cb_ = std::move(cb); }

        TcpSession(uv_loop_t *loop, UdpDriver *udp_driver);

        // lifetime control
        void connect(); // three handshake
        void close(); // four handshake
        void listen(); // only for server, transition to LISTEN state, wait for incoming connection request
        bool is_connected() const;

        bool is_closed() const;

        // data transmission
        void input(const myu::myu_tcp_packet &packet); // the entry function for handling incoming packet
        size_t send(std::span<const uint8_t> data);

        size_t recv(std::span<uint8_t> buf);

        std::span<uint8_t> read_all();

        size_t available() const; // return the size of data that can be read from recv_buffer_

        // callbacks
        void set_on_established(std::function<void()> callback);

        void set_on_data(std::function<void(size_t)> callback);

        void set_on_closed(std::function<void()> callback);

        void set_on_error(std::function<void(const std::string &)> callback);

        void set_on_recv_three_dup_ack(std::function<void(uint32_t ack_num)> callback);

        // state utils
        TcpState get_state() const;

        std::pair<std::string, uint16_t> get_listen_addr() const;

        std::pair<std::string, uint16_t> get_remote_addr() const;

        uint32_t get_peer_usable_recv_window_size() const {
            return peer_usable_window_size_;
        }

        uint32_t get_send_window_size() const {
            return send_window_.send_window_size_;
        }

        uint32_t get_recv_window_size() const {
            return recv_window_.recv_window_size_;
        }

        uint32_t get_usable_send_window_size() const {
            return send_window_.get_usable_window_size();
        }

        uint32_t get_usable_recv_window_size() const {
            return recv_buffer_.capacity() - recv_buffer_.size();
        }

        // handle fsm
        // client
        void handle_syn_sent(const myu::myu_tcp_packet &packet); // only handle SYN-ACK packet
        void handle_fin_wait_1(const myu::myu_tcp_packet &packet); // only handle ACK and FIN packet
        void handle_fin_wait_2(const myu::myu_tcp_packet &packet); // only handle FIN packet
        void handle_timed_wait(const myu::myu_tcp_packet &packet); // only handle ACK packet
        // server
        void handle_listen(const myu::myu_tcp_packet &packet); // only handle SYN packet
        void handle_syn_received(const myu::myu_tcp_packet &packet); // only handle ACK packet
        void handle_close_wait(const myu::myu_tcp_packet &packet); // only handle FIN packet
        void handle_last_ack(const myu::myu_tcp_packet &packet); // only handle ACK packet
        // common
        void handle_established(const myu::myu_tcp_packet &packet); // only handle ACK and data packet
        // urgent
        void handle_reset(); // only handle RST packet

        uint64_t get_timeout_ms() const { return current_rto_; }

        // ip-port utils
        std::string get_listener_ip() const { return listener_ip_; }
        uint16_t get_listener_port() const { return listener_port_; }
        std::string get_remote_ip() const { return remote_ip_; }
        uint16_t get_remote_port() const { return remote_port_; }

        // the listener_ip is a std::string, this will cause deep copy
        void set_listener_addr(const char *ip, uint16_t port) {
            listener_ip_ = ip;
            listener_port_ = port;
        }

        void set_remote_addr(const char *ip, uint16_t port) {
            remote_ip_ = ip;
            remote_port_ = port;
        }

        constexpr static uint32_t MAX_RETRANSMIT_COUNT = 5;
    };
}
