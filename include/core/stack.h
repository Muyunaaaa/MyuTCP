#pragma once
#include <memory>

#include "tcb.h"
#include "TimerManager.h"
#include "UdpDriver.h"
#include "uv.h"

/*
 *那就这样，我设置一个session的管理器，拥有一个session的容器这里面放着所有我管理的session，还有一个udp_driver，然后每个session中拥有一个timer_manager_，
 *然后用户的接口有createsession，listen（像你刚才说的，如果监听到之后，如果session已经存在（根据remote_addr作为判断依据），
 *直接在这个基础上进行通信就可以，如果没有，就把创建新的session并且把remote_addr放进去，这样的话我们也不需要在handle_listen设置远程地址了），
 *还有destroy，关闭所有session，释放所有资源
 */
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
