#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <winsock2.h>

namespace nbrelay::wire {

enum class FrameType : uint8_t {
    hello = 1,
    data = 2,
    close = 3,
    ping = 4
};

struct Frame {
    FrameType type = FrameType::data;
    uint32_t channel = 1;
    std::vector<uint8_t> payload;
};

bool send_all(SOCKET socket, const char* data, int len);
bool recv_all(SOCKET socket, char* data, int len);

bool write_frame(SOCKET socket, const Frame& frame);
bool read_frame(SOCKET socket, Frame& frame, uint32_t max_payload_bytes);

Frame hello_frame(const std::string& text);
Frame data_frame(const char* bytes, int len);
Frame close_frame();
std::string payload_text(const Frame& frame);

} // namespace nbrelay::wire
