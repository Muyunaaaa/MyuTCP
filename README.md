# MyuTCP

**MyuTCP** is a lightweight, user-space TCP protocol stack implemented based on `libuv`, featuring the core characteristics of the standard TCP protocol.



## Core Features

- **Full Handshake/Teardown Logic**: Implements standard TCP state machines for Three-way Handshake and Four-way Teardown.
- **Reliable Transmission**: Supports data validation and retransmission based on Sequence Numbers (Seq) and Acknowledgment Numbers (Ack).
- **Congestion Control**: Built-in **TCP Reno** algorithm, including Slow Start, Congestion Avoidance, Fast Retransmit, and Fast Recovery.
- **Asynchronous Event-Driven**: Built on `libuv`, perfectly integrating into asynchronous IO event loops.



## Installation & Integration

MyuTCP is recommended to be integrated into your CMake project as a Git Submodule.

### 1. Add Submodule

Run the following in your project root directory:

```bash
git submodule add https://github.com/Muyunaaaa/MyuTCP.git extern/MyuTCP
git submodule update --init --recursive
```

### 2. Configure CMake

Add the following to your `CMakeLists.txt`:

```cmake
add_subdirectory(extern/MyuTCP)

add_executable(your_target main.cpp)

target_link_libraries(your_target PRIVATE myutcp)
```

### 3. Optional Build Configurations

During the CMake configuration phase, you can choose whether to enable internal debug logging:

- `-DMYU_DEBUG_LOG=ON`: Enables detailed protocol stack tracking logs (Seq/Ack, sliding window status, etc.).
- `-DMYU_DEBUG_LOG=OFF`: (Default) Disables logging for maximum performance.

### 4. Include Header

```c++
#include "myutcp/myutcp.h"
```



## Basic API Overview

### `myu::TcpStack`

The execution container of the protocol stack, responsible for managing the event loop and the underlying UDP driver.

- `TcpStack(const char* ip, uint16_t port)`: Initializes the stack and binds it to a local UDP address.
- `create_session(remote_ip, remote_port)`: Creates a new client session.
- `listen()`: Sets the stack to listening mode, ready to accept incoming connections.
- `run()`: Starts the `libuv` event loop (blocking call).

### `myu::TcpSession`

Represents a single TCP connection.

- `connect()`: Initiates the three-way handshake.
- `send(std::span<const uint8_t>)`: Sends a stream of bytes.
- `read_all()`: Retrieves all available data from the receive buffer.
- `close()`: Initiates the four-way teardown to close the connection.
- `set_app_logic(callback)`: Sets the application layer callback, triggered upon receiving data or state changes.

------



## Code Examples

### Client Example

```c++
#include "myutcp/myutcp.h"

int main() {
    // Initialize stack (local binding)
    auto *client = new myu::TcpStack("127.0.0.1", 9999);
    
    // Create session and connect to server
    auto *session = client->create_session("127.0.0.1", 10000);
    session->connect();

    // Send data
    std::string message = "Hello, Server!";
    session->send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(message.data()), message.size()));

    // Example: Close session after 10 seconds using a libuv timer
    uv_timer_t delay_req;
    uv_timer_init(client->loop_, &delay_req);
    delay_req.data = session;
    uv_timer_start(&delay_req, [](uv_timer_t* handle) {
        auto* s = static_cast<myu::TcpSession*>(handle->data);
        s->close();
        uv_timer_stop(handle);
    }, 10000, 0);

    client->run();
}
```

### Server Example

```c++
#include "myutcp/myutcp.h"

int main() {
    // Initialize stack and start listening
    myu::TcpStack *server = new myu::TcpStack("127.0.0.1", 10000);
    server->listen();

    // Define application logic
    server->set_app_logic([](myu::TcpSession *s) {
        auto data = s->read_all();
        if (!data.empty()) {
            spdlog::info("Received {} bytes from {}:{}", data.size(), s->get_remote_ip(), s->get_remote_port());
        } else if (s->get_state() == myu::TcpState::CLOSE_WAIT) {
            spdlog::info("Peer initiated closure.");
        }
    });

    server->run();
}
```
