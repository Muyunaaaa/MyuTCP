#pragma once
#include "uv.h"
#include <functional>
#include "segment.h"

using OnRecCallBack = std::function<void(const myu::myu_tcp_packet&, const sockaddr_in&)>;

struct UdpDriver {
public:
    UdpDriver(uv_loop_t* loop, const char* listen_ip, int listen_port);
    ~UdpDriver() = default;

    // set callback function when receive packet
    inline void set_on_receive(OnRecCallBack callback){on_receive_callback_ = std::move(callback);}

    void send_packet(const myu::myu_tcp_packet& packet, const sockaddr_in& dest_addr);

    inline void set_loss_rate(float rate){loss_rate_ = rate;}
private:
    uv_udp_t udp_handle_;
    OnRecCallBack on_receive_callback_;
    float loss_rate_ = 0.0f;

    // the callback function when receive packet, it will be called by libuv
    static void on_uv_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                               const sockaddr* addr, unsigned flags);
    static void on_uv_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
};
