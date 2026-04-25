#include "myutcp/myutcp.h"
#include "spdlog/spdlog.h"

void UdpDriver::init(uv_loop_t* loop, const char* listen_ip, int listen_port) {
    int r;
    udp_handle_ = new uv_udp_t();
    r = uv_udp_init(loop, udp_handle_);
    if (r < 0) MYU_LOG_ERROR("Init error: {}", uv_strerror(r));

    udp_handle_->data = this;
    struct sockaddr_in listen_addr;
    uv_ip4_addr(listen_ip, listen_port, &listen_addr);

    r = uv_udp_bind(udp_handle_, (const struct sockaddr *)&listen_addr, UV_UDP_REUSEADDR);
    if (r < 0) MYU_LOG_ERROR("Bind error: {}", uv_strerror(r));

    r = uv_udp_recv_start(udp_handle_, on_uv_alloc, on_uv_recv);
    if (r < 0) MYU_LOG_ERROR("Recv start error: {}", uv_strerror(r));

    MYU_LOG_INFO("TDP Driver started on {}:{}", listen_ip, listen_port);

}

void UdpDriver::send_packet(std::shared_ptr<myu::myu_tcp_packet> packet, const sockaddr_in &dest_addr) {
    // simulate packet loss
    if ((static_cast<float>(rand()) / RAND_MAX) < loss_rate_) {
        MYU_LOG_WARN("Packet seq={} LOST (Simulated)", packet.get()->header.seq_num);
        return;
    }

    struct SendCtx {
        uv_udp_send_t req;
        std::vector<uint8_t> raw_data;
        uv_buf_t buf;
        sockaddr_in dest_addr;
    };
    SendCtx* ctx = new SendCtx();
    size_t header_size = sizeof(packet->header);
    size_t payload_size = packet->payload.size();
    ctx->raw_data.resize(header_size + payload_size);

    memcpy(ctx->raw_data.data(), &packet->header, header_size);
    if (payload_size > 0) {
        memcpy(ctx->raw_data.data() + header_size, packet->payload.data(), payload_size);
    }

    ctx->buf = uv_buf_init(reinterpret_cast<char*>(ctx->raw_data.data()), ctx->raw_data.size());
    ctx->req.data = ctx;
    ctx->dest_addr = dest_addr;

    sockaddr_in dest_addr_copy;
    memcpy(&dest_addr_copy, &dest_addr, sizeof(sockaddr_in));


    int r = uv_udp_send(&ctx->req, udp_handle_, &ctx->buf, 1,
                           reinterpret_cast<const sockaddr*>(&ctx->dest_addr),
                           [](uv_udp_send_t* req, int status) {
            SendCtx* sc = static_cast<SendCtx*>(req->data);
            if (status < 0) {
                MYU_LOG_ERROR("UDP send async error: {}", uv_strerror(status));
            }
            delete sc;
        });

    if (r < 0) {
        MYU_LOG_ERROR("uv_udp_send immediate failed: {}", uv_strerror(r));
        delete ctx;
    }
}

void UdpDriver::on_uv_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = new char[suggested_size];
    buf->len = suggested_size;
}

void UdpDriver::on_uv_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                           const sockaddr* addr, unsigned flags) {
    UdpDriver* driver = static_cast<UdpDriver*>(handle->data);

    if (nread > 0 && addr != nullptr && driver->on_receive_callback_) {
        const size_t header_size = sizeof(myu::myu_tcp_header);

        if (nread >= (ssize_t)header_size) {
            myu::myu_tcp_packet packet;

            memcpy(&packet.header, buf->base, header_size);

            size_t payload_size = nread - header_size;
            if (payload_size > 0) {
                packet.payload.assign(
                    reinterpret_cast<uint8_t*>(buf->base + header_size),
                    reinterpret_cast<uint8_t*>(buf->base + nread)
                );
            }
            driver->on_receive_callback_(packet, *reinterpret_cast<const sockaddr_in*>(addr));
        }
    }

    if (buf->base) delete[] buf->base;
}
