#include "stack.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"

int main() {
    auto client_logger = spdlog::stdout_color_mt("client_logger");
    client_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [CLIENT] [%^%l%$] %v");

    myu::TcpStack *client = new myu::TcpStack("127.0.0.1", 9999);
    client_logger->info("Client connecting to server...");

    auto *session = client->create_session("127.0.0.1", 10000);

    session->connect();

    std::string message = "Hello, Server!";
    session->send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(message.data()), message.size()));

    // uv_timer_t delay_req;
    // uv_timer_init(client->loop_, &delay_req);
    // delay_req.data = session;
    //
    // uv_timer_start(&delay_req, [](uv_timer_t* handle) {
    //     auto* s = static_cast<myu::TcpSession*>(handle->data);
    //     spdlog::info("Delay finished, now closing session...");
    //     s->close();
    //
    //     uv_timer_stop(handle);
    // }, 10000, 0);

    // fixme: when the user call close function, whatever the state is, we just transition to CLOSED state and close the session
    session->close();

    client->run();
}