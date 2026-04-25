#include "myutcp/myutcp.h"

int main() {
    auto *client = new myu::TcpStack("127.0.0.1", 9999);
    spdlog::info("Client connecting to server...");

    auto *session = client->create_session("127.0.0.1", 10000);

    session->connect();

    std::string message = "Hello, Server!";
    session->send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(message.data()), message.size()));

    uv_timer_t delay_req;
    uv_timer_init(client->loop_, &delay_req);
    delay_req.data = session;

    uv_timer_start(&delay_req, [](uv_timer_t* handle) {
        auto* s = static_cast<myu::TcpSession*>(handle->data);
        spdlog::info("Delay finished, now closing session...");
        s->close();

        uv_timer_stop(handle);
    }, 10000, 0);


    client->run();
}