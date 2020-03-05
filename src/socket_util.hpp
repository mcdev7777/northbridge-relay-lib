#pragma once

#include <cstdint>
#include <string>
#include <winsock2.h>

namespace nbrelay::net {

class WinsockRuntime {
public:
    WinsockRuntime();
    ~WinsockRuntime();

    WinsockRuntime(const WinsockRuntime&) = delete;
    WinsockRuntime& operator=(const WinsockRuntime&) = delete;

    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

SOCKET make_listener(const std::string& bind_address, uint16_t port, std::string* error);
SOCKET connect_tcp(const std::string& host, uint16_t port, std::string* error);
void close_socket(SOCKET socket);
void set_nodelay(SOCKET socket);
std::string last_socket_error(const char* where);

} // namespace nbrelay::net
