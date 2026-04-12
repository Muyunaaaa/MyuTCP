#include "stack.h"

int main() {
    myu::TcpStack stack2 = myu::TcpStack("0.0.0.0",9999);
    myu::TcpStack stack1 = myu::TcpStack("0.0.0.0",10000);
    myu::TcpSession* session = stack1.create_session("127.0.0.1", 9999);
    stack2.listen();
    session->connect();
    uint8_t arr[] = {10, 20, 30, 40};
    std::span<uint8_t> s2{arr};
    session->send(s2);
    stack1.run();
    stack2.run();
}
