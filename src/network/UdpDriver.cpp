#include "UdpDriver.h"

#include "spdlog/spdlog.h"
#include "util/parse_ip.h"
void UdpDriver::init(uv_loop_t* loop, const char* listen_ip, int listen_port) {
    int r;
    r = uv_udp_init(loop, &udp_handle_);
    if (r < 0) spdlog::error("Init error: {}", uv_strerror(r));

    udp_handle_.data = this;
    struct sockaddr_in listen_addr;
    uv_ip4_addr(listen_ip, listen_port, &listen_addr);

    r = uv_udp_bind(&udp_handle_, (const struct sockaddr *)&listen_addr, UV_UDP_REUSEADDR);
    if (r < 0) spdlog::error("Bind error: {}", uv_strerror(r));

    r = uv_udp_recv_start(&udp_handle_, on_uv_alloc, on_uv_recv);
    if (r < 0) spdlog::error("Recv start error: {}", uv_strerror(r));

    spdlog::info("UDP Driver started on {}:{}", listen_ip, listen_port);
}

void UdpDriver::send_packet(std::shared_ptr<myu::myu_tcp_packet> packet, const sockaddr_in &dest_addr) {
    // simulate packet loss
    if ((static_cast<float>(rand()) / RAND_MAX) < loss_rate_) {
        spdlog::warn("Packet seq={} LOST (Simulated)", packet.get()->header.seq_num);
        return;
    }

    struct Ctx {
            std::shared_ptr<myu::myu_tcp_packet> packet;
            uv_buf_t* buf;
    };
    spdlog::info("Actually sending to {}:{}", _get_ip_str(dest_addr), ntohs(dest_addr.sin_port));
    uv_buf_t* heap_buf = new uv_buf_t(uv_buf_init((char*)packet.get(), sizeof(*packet)));
    uv_udp_send_t *send_req = new uv_udp_send_t();
    Ctx* context = new Ctx(packet, heap_buf);
    spdlog::info("test: checkpoint 1");
    send_req->data = context;
    uv_udp_send(send_req, &udp_handle_, heap_buf, 1, reinterpret_cast<const sockaddr *>(&dest_addr), [](uv_udp_send_t *req, int status) {
        if (status < 0) {
            spdlog::error("UDP send error: {}", uv_strerror(status));
        }
        Ctx* ctx = (Ctx*)req->data;
        delete ctx;
        delete req;
    });
}

void UdpDriver::on_uv_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = new char[suggested_size];
    buf->len = suggested_size;
}

void UdpDriver::on_uv_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                           const sockaddr* addr, unsigned flags) {
    UdpDriver* driver = static_cast<UdpDriver*>(handle->data);

    if (nread > 0 && addr != nullptr && driver->on_receive_callback_) {
        // check size of received data, it should be at least the size of TcpHeader
        if (nread >= 20) {
            myu::myu_tcp_packet packet;
            memcpy(&packet, buf->base, sizeof(myu::myu_tcp_packet));

            driver->on_receive_callback_(packet, *reinterpret_cast<const sockaddr_in*>(addr));
        }
    }

    if (buf->base) delete[] buf->base;
}
