#include "stack.h"

int main() {
    myu::TcpStack stack = myu::TcpStack("0.0.0.0",10000);
    stack.listen();
    stack.run();
}
