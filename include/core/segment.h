#pragma once
#include <vector>


namespace myu {
#pragma pack(push, 1)
    struct myu_tcp_header {
        // source port
        uint16_t s_port;
        // destination port
        uint16_t d_port;
        // sequence number
        uint32_t seq_num;
        // ack number
        uint32_t ack_num;

        // // header length
        // uint8_t hl : 4;
        // // reserved length
        // uint8_t reserved : 4;

        // //flags
        // uint8_t  cwr : 1;
        // uint8_t  ece : 1;
        // uint8_t  urg : 1; // urgent data
        // uint8_t  ack : 1;
        // uint8_t  psh : 1; // push imm
        // uint8_t  rst : 1; // force to close
        // uint8_t  syn : 1; // establish connection
        // uint8_t  fin : 1; // apply to close

        uint8_t  data_offset_and_reserved; // 4 higher bits is hl, 4 lower bits is reserved
        uint8_t  flags;// 8 bits for flags, the order of flags is cwr, ece, urg, ack, psh, rst, syn, fin

        uint16_t window_size;       // size of bytes can be accepted
        uint16_t checksum;
        uint16_t urgent_ptr;
    };

    static_assert(sizeof(myu_tcp_header) == 20, "TcpHeader size must be exactly 20 bytes");

#pragma pack(pop)

    struct myu_tcp_packet {
        myu_tcp_header header;
        std::vector<uint8_t> options;
        std::vector<uint8_t> payload;
    };
}
