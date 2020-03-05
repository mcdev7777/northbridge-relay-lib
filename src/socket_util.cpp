#include "socket_util.hpp"

#include <ws2tcpip.h>
#include <sstream>

namespace nbrelay::net {

WinsockRuntime::WinsockRuntime() {
    WSADATA data{};
    ok_ = (::WSAStartup(MAKEWORD(2, 2), &data) == 0);
}

WinsockRuntime::~WinsockRuntime() {
    if (ok_) {
        ::WSACleanup();
    }
}

std::string last_socket_error(const char* where) {
    std::ostringstream out;
    out << where << " failed: WSA error " << ::WSAGetLastError();
    return out.str();
}

void close_socket(SOCKET socket) {
    if (socket != INVALID_SOCKET) {
        ::shutdown(socket, SD_BOTH); // noisy if already closed, but harmless here
        ::closesocket(socket);
    }
}

void set_nodelay(SOCKET socket) {
    BOOL yes = TRUE;
    ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
}

SOCKET make_listener(const std::string& bind_address, uint16_t port, std::string* error) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* result = nullptr;
    std::string port_text = std::to_string(port);
    int rc = ::getaddrinfo(bind_address.c_str(), port_text.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
        if (error) *error = "getaddrinfo failed for listener";
        return INVALID_SOCKET;
    }

    SOCKET s = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (s == INVALID_SOCKET) {
        if (error) *error = last_socket_error("socket");
        ::freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    BOOL reuse = TRUE;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    if (::bind(s, result->ai_addr, static_cast<int>(result->ai_addrlen)) == SOCKET_ERROR) {
        if (error) *error = last_socket_error("bind");
        close_socket(s);
        ::freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    if (::listen(s, SOMAXCONN) == SOCKET_ERROR) {
        if (error) *error = last_socket_error("listen");
        close_socket(s);
        ::freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    ::freeaddrinfo(result);
    return s;
}

SOCKET connect_tcp(const std::string& host, uint16_t port, std::string* error) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    std::string port_text = std::to_string(port);
    int rc = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
        if (error) *error = "getaddrinfo failed for " + host;
        return INVALID_SOCKET;
    }

    SOCKET s = INVALID_SOCKET;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        s = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (s == INVALID_SOCKET) continue;

        if (::connect(s, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0) {
            set_nodelay(s);
            ::freeaddrinfo(result);
            return s;
        }

        close_socket(s);
        s = INVALID_SOCKET;
    }

    if (error) *error = last_socket_error("connect");
    ::freeaddrinfo(result);
    return INVALID_SOCKET;
}

} // namespace nbrelay::net
