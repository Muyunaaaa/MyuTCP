#include "TimerManager.h"
#include "UdpDriver.h"
#include "spdlog/spdlog.h"

void safe_send_task(uint32_t seq, myu::myu_tcp_packet packet, sockaddr_in dest,
                    UdpDriver& driver, TimerManager& timer_manager) {

    spdlog::info("[Test] Attempting to send packet seq: {}", seq);
    driver.send_packet(packet, dest);

    timer_manager.start_timer(seq, 2000, [=, &driver, &timer_manager]() {
        safe_send_task(seq, packet, dest, driver, timer_manager);
    });
}

int main() {
    uv_loop_t *loop = uv_default_loop();
    UdpDriver driver(loop, "127.0.0.1", 9999);
    TimerManager timer_manager(loop);

    driver.set_loss_rate(0.2f);
    driver.set_on_receive([&timer_manager](const myu::myu_tcp_packet& packet, const sockaddr_in& addr) {
            spdlog::info("--- [Test] Received packet! seq: {} ---", packet.header.seq_num);

            timer_manager.stop_timer(packet.header.seq_num);
        });

    spdlog::info("Test loop started. Press Ctrl+C to stop.");

    // Simulate sending packets
    for (uint32_t seq = 1; seq <= 5; ++seq) {
        myu::myu_tcp_packet packet;
        packet.header.seq_num = seq;
        packet.header.ack_num = 0;
        packet.header.s_port = 12345;
        packet.header.d_port = 9999;
        packet.header.hl = 5;
        packet.header.reserved = 0;
        packet.header.cwr = 0;
        packet.header.ece = 0;
        packet.header.urg = 0;
        packet.header.ack = 0;
        packet.header.psh = 0;
        packet.header.rst = 0;
        packet.header.syn = 1;
        packet.header.fin = 0;
        packet.header.window_size = 1024;
        packet.header.checksum = 0;
        packet.header.urgent_ptr = 0;

        sockaddr_in dest;
        uv_ip4_addr("127.0.0.1", 9999, &dest);

        // recursive lambda for retransmission
        safe_send_task(seq, packet, dest, driver, timer_manager);
    }

    return uv_run(loop, UV_RUN_DEFAULT);
}
