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
        std::map<std::pair<std::string, uint16_t>, std::unique_ptr<TcpSession> > tcp_sessions_;

    public:
        uv_loop_t *loop_;
        UdpDriver *udp_driver_;

        TcpStack(const TcpStack &) = delete;

        TcpStack &operator=(const TcpStack &) = delete;

        TcpStack(const char *listen_ip, uint16_t listen_port) {
            loop_ = new uv_loop_t;
            tcp_sessions_ = std::map<std::pair<std::string, uint16_t>, std::unique_ptr<TcpSession> >();
            uv_loop_init(loop_);
            udp_driver_ = new UdpDriver();
            udp_driver_->init(loop_, listen_ip, listen_port);
        }

        ~TcpStack() {
            uv_loop_close(loop_);
            delete loop_;
        }

        TcpSession *create_session(std::string remote_ip, uint16_t remote_port) {
            auto new_session = std::make_unique<TcpSession>(loop_, udp_driver_);

            new_session->set_remote_addr(remote_ip.c_str(), remote_port);

            TcpSession *session_ptr = new_session.get();

            // this function can be override by user, user can make a buffer as the outer var reference to get the data
            new_session->set_on_data([session_ptr](size_t available) {
                std::vector<uint8_t> buffer(available);
                size_t n = session_ptr->recv(buffer);
                // when the session recive the data, we just print the data to the console, in real application, user can do whatever they want with the data
                spdlog::info("Received data from {}:{}. Data size = {}, content = {}",
                             session_ptr->get_remote_ip(), session_ptr->get_remote_port(), n,
                             std::string(buffer.begin(), buffer.end()));
            });

            // this function can be override by user, when the session is closed, the user can do some clean work in this callback function
            new_session->set_on_closed([this, remote_ip, remote_port]() {
                // destroy and erase the session from tcp_sessions_ when the session is closed
                spdlog::info("Stack: Removing session {}:{}", remote_ip, remote_port);
                auto key = std::make_pair(remote_ip, remote_port);
                this->tcp_sessions_.erase(key);
            });

            spdlog::info("Create a session, the remote's ip = {} and port = {}", new_session->get_remote_ip(),
                         new_session->get_remote_port());

            auto key = std::make_pair(remote_ip, remote_port);

            auto [it, inserted] = tcp_sessions_.try_emplace(std::move(key), std::move(new_session));

            return it->second.get();
        }

        void listen() {
            // get the remote addr
            udp_driver_->set_on_receive([&](const myu::myu_tcp_packet &packet, const sockaddr_in &addr) {
                spdlog::info("Received packet from {}:{}", _get_ip_str(addr), ntohs(addr.sin_port));
                std::string remote_ip = _get_ip_str(addr);
                uint16_t remote_port = ntohs(addr.sin_port);

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
                    if (packet.header.flags & FLAG_SYN) {
                        spdlog::info("Received SYN packet from {}:{}. Create a new session for this connection.",
                                     remote_ip, remote_port);
                        TcpSession *new_session = create_session(remote_ip, remote_port);
                        new_session->set_remote_addr(remote_ip.c_str(), remote_port);
                        new_session->listen();
                        new_session->input(packet);
                    } else {
                        // send rst to peer
                        sockaddr_in dest;
                        uv_ip4_addr(remote_ip.c_str(), remote_port, &dest);
                        std::shared_ptr<myu::myu_tcp_packet> packet_ = std::make_shared<myu::myu_tcp_packet>();
                        packet_.get()->header.flags = FLAG_RST;
                        udp_driver_->send_packet(packet_, dest);
                    }
                }
            });
        }

        TcpSession *get_session(std::string remote_ip, uint16_t remote_port) {
            auto it = tcp_sessions_.find(std::make_pair(remote_ip, remote_port));
            if (it != tcp_sessions_.end()) {
                return it->second.get();
            } else {
                return nullptr;
            }
        }

        void run() {
            uv_run(loop_, UV_RUN_DEFAULT);
        }

        // 1. stop all sessions
        // 2. release udp_driver and udp_loop_t
        // 3. remove all sessions from contains
        void stop_and_destory() {
            for (auto &it: tcp_sessions_) {
                it.second->close();
            }
            udp_driver_->stop();
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
