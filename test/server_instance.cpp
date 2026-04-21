#include "stack.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
int main() {
    auto server_logger = spdlog::stdout_color_mt("client_logger");
    server_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [CLIENT] [%^%l%$] %v");

    myu::TcpStack *server = new myu::TcpStack("127.0.0.1", 10000);
    server_logger->info("Client connecting to server...");


    server->listen();

    // new_session->set_on_data([session_ptr](size_t available) {
    // std::vector<uint8_t> buffer(available);
    // size_t n = session_ptr->recv(buffer);
    // // when the session recive the data, we just print the data to the console, in real application, user can do whatever they want with the data
    // spdlog::info("Received data from {}:{}. Data size = {}, content = {}",
    //              session_ptr->get_remote_ip(),session_ptr->get_remote_port(), n,
    //              std::string(buffer.begin(), buffer.end()));
    // });

    server->run();
}