#include "stack.h"

int main() {
    myu::TcpStack stack = myu::TcpStack("0.0.0.0",9999);
    myu::TcpSession* session = stack.create_session("192.168.2.103", 10000);
    session->connect();
    uint8_t arr[] = {10, 20, 30, 40};
    std::span<uint8_t> s2{arr};
    session->send(s2);
    stack.run();
}
