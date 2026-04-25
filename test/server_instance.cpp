#include "stack.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"

int main() {
    auto server_logger = spdlog::stdout_color_mt("client_logger");
    server_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [SERVER] [%^%l%$] %v");

    myu::TcpStack *server = new myu::TcpStack("127.0.0.1", 10000);
    server_logger->info("Server is listening");


    server->listen();

    std::string global_buffer;
    server->set_app_logic([&global_buffer](myu::TcpSession *s) {
        auto data = s->read_all();
        std::string chunk(data.begin(), data.end());
        global_buffer += chunk;
        uint32_t n = data.size();
        if (n > 0) {
            spdlog::info("Received {} bytes from {}:{}", n, s->get_remote_ip(), s->get_remote_port());
            std::string msg(data.begin(), data.begin() + n);
            spdlog::info("Message: {}", msg);
            spdlog::info("The entire massage : {}", global_buffer);
        } else if (s->get_state() == myu::TcpState::CLOSE_WAIT) {
            spdlog::info("Client disconnected.");
        }
    });


    server->run();
}
