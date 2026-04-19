#include "stack.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"

int main() {
    auto server_logger = spdlog::stdout_color_mt("server_logger");
    server_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [SERVER] [%^%l%$] %v");

    myu::TcpStack *server = new myu::TcpStack("127.0.0.1", 10000);
    server_logger->info("Server initializing...");
    server->listen();
    server->run();

    return 0;
}
