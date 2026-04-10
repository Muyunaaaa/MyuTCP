#pragma once
#include <memory>

#include "tcb.h"
#include "TimerManager.h"
#include "UdpDriver.h"
#include "uv.h"

namespace myu {
    class TcpStack {
    private:
        uv_loop_t *loop_;

        TimerManager timer_manager_;
        UdpDriver udp_driver_;
    public:
        TcpStack(const char *listen_ip, uint16_t listen_port)
            : loop_(new uv_loop_t), timer_manager_(loop_), udp_driver_(loop_, listen_ip, listen_port) {
            uv_loop_init(loop_);
        }

        ~TcpStack() {
            uv_loop_close(loop_);
            delete loop_;
        }

        std::unique_ptr<TcpSession> create_session() {
            return std::make_unique<TcpSession>(timer_manager_, udp_driver_);
        }

        void run() {
            uv_run(loop_, UV_RUN_DEFAULT);
        }
    };
}
