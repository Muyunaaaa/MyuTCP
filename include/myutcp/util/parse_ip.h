#pragma once
#include <string>
#include <ws2tcpip.h>

inline std::string _get_ip_str(const struct sockaddr_in &addr) {
    char ip_buffer[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_buffer, sizeof(ip_buffer)) != nullptr) {
        return std::string(ip_buffer);
    }

    return "Unknown";
}
