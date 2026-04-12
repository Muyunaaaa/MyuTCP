#pragma once
#include <memory>

#include "tcb.h"
#include "TimerManager.h"
#include "UdpDriver.h"
#include "uv.h"
#include "util/parse_ip.h"

namespace myu {
    class TcpStack {
    private:
        uv_loop_t *loop_;
        std::map<std::pair<std::string, uint16_t>, std::unique_ptr<TcpSession> > tcp_sessions_;
        UdpDriver udp_driver_;

    public:
        TcpStack(const char *listen_ip, uint16_t listen_port) {
            loop_ = new uv_loop_t;
            tcp_sessions_ = std::map<std::pair<std::string, uint16_t>, std::unique_ptr<TcpSession> >();
            uv_loop_init(loop_);
            udp_driver_ = UdpDriver();
            udp_driver_.init(loop_, listen_ip, listen_port);
        }

        ~TcpStack() {
            uv_loop_close(loop_);
            delete loop_;
        }

        TcpSession *create_session(std::string remote_ip, uint16_t remote_port) {
            auto new_session = std::make_unique<TcpSession>(loop_, udp_driver_);

            new_session->set_remote_addr(remote_ip.c_str(), remote_port);

            spdlog::info("test: now the ip = {} and port = {}", new_session->get_remote_ip(), new_session->get_remote_port());

            auto key = std::make_pair(remote_ip, remote_port);

            auto [it, inserted] = tcp_sessions_.try_emplace(std::move(key), std::move(new_session));

            return it->second.get();
        }

        void listen() {
            // get the remote addr
            std::string remote_ip;
            uint16_t remote_port;
            udp_driver_.set_on_receive([&](const myu::myu_tcp_packet &packet, const sockaddr_in &addr) {
                spdlog::info("Received packet from {}:{}", _get_ip_str(addr), ntohs(addr.sin_port));
                remote_ip = _get_ip_str(addr);
                remote_port = ntohs(addr.sin_port);

                // check the remote addr whether in the local sessions
                // if the addr exists, then open this session, verify the session state and use input function
                // otherwise, create a new session and use verify the session and use input function
                auto it = tcp_sessions_.find(std::make_pair(remote_ip, remote_port));
                if (it != tcp_sessions_.end()) {
                    // it means that the session exists
                    it->second->input(packet);
                } else {
                    // only the syn packet would enter this branch
                    // if the packet is not syn packet, it means that some errors occurred.
                    if (packet.header.syn) {
                        TcpSession *new_session = create_session(remote_ip, remote_port);
                        new_session->set_remote_addr(remote_ip.c_str(), remote_port);
                        new_session->input(packet);
                    } else {
                        // send rst to peer
                        sockaddr_in dest;
                        uv_ip4_addr(remote_ip.c_str(), remote_port, &dest);
                        myu::myu_tcp_packet packet_;
                        packet_.header.rst = 1;
                        udp_driver_.send_packet(packet, dest);
                    }
                }
            });
        }

        void recv() {

        }
        void run() {
            uv_run(loop_, UV_RUN_DEFAULT);
        }

        // 1. stop all sessions
        // 2. release udp_driver and udp_loop_t
        // 3. remove all sessions from contains
        void stop_and_destory() {
            for (auto& it : tcp_sessions_) {
                it.second->close();
            }
            udp_driver_.stop();
            tcp_sessions_.clear();
            while (uv_loop_alive(loop_)) {
                uv_run(loop_, UV_RUN_ONCE);
            }

            if (uv_loop_close(loop_) == UV_EBUSY) {
                spdlog::error("Loop closed while busy! Some handles might not be closed.");
            }
        }
    };
}
