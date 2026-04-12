#pragma once
#include "uv.h"
#include <functional>
#include "segment.h"
#include "spdlog/spdlog.h"

using OnRecCallBack = std::function<void(const myu::myu_tcp_packet&, const sockaddr_in&)>;

struct UdpDriver {
public:
    UdpDriver() = default;
    ~UdpDriver() = default;

    void init(uv_loop_t* loop, const char* listen_ip, int listen_port);

    // set callback function when receive packet
    inline void set_on_receive(OnRecCallBack callback){on_receive_callback_ = std::move(callback);}

    void send_packet(std::shared_ptr<myu::myu_tcp_packet> packet, const sockaddr_in& dest_addr);

    inline void set_loss_rate(float rate){loss_rate_ = rate;}

    void stop() {
        if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&udp_handle_))) {
            uv_close(reinterpret_cast<uv_handle_t*>(&udp_handle_), [](uv_handle_t* handle) {
                spdlog::info("UdpDriver stopped.");
            });
        }
    }
private:
    uv_udp_t udp_handle_;
    OnRecCallBack on_receive_callback_;
    float loss_rate_ = 0.0f;

    // the callback function when receive packet, it will be called by libuv
    static void on_uv_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                               const sockaddr* addr, unsigned flags);
    static void on_uv_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
};
