#include "wire.hpp"

#include <array>
#include <cstring>

namespace nbrelay::wire {

namespace {
constexpr char kMagic0 = 'N';
constexpr char kMagic1 = 'B';
constexpr uint8_t kVersion = 1;
constexpr uint32_t kHeaderBytes = 12;

void put_u32(char* dst, uint32_t value) {
    uint32_t n = htonl(value);
    std::memcpy(dst, &n, sizeof(n));
}

uint32_t get_u32(const char* src) {
    uint32_t n = 0;
    std::memcpy(&n, src, sizeof(n));
    return ntohl(n);
}
}

bool send_all(SOCKET socket, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = ::send(socket, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool recv_all(SOCKET socket, char* data, int len) {
    int got = 0;
    while (got < len) {
        int n = ::recv(socket, data + got, len - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

bool write_frame(SOCKET socket, const Frame& frame) {
    if (frame.payload.size() > 16 * 1024 * 1024) {
        return false;
    }

    std::array<char, kHeaderBytes> header{};
    header[0] = kMagic0;
    header[1] = kMagic1;
    header[2] = static_cast<char>(kVersion);
    header[3] = static_cast<char>(frame.type);
    put_u32(header.data() + 4, frame.channel);
    put_u32(header.data() + 8, static_cast<uint32_t>(frame.payload.size()));

    if (!send_all(socket, header.data(), static_cast<int>(header.size()))) {
        return false;
    }

    if (!frame.payload.empty()) {
        return send_all(socket,
                        reinterpret_cast<const char*>(frame.payload.data()),
                        static_cast<int>(frame.payload.size()));
    }
    return true;
}

bool read_frame(SOCKET socket, Frame& frame, uint32_t max_payload_bytes) {
    std::array<char, kHeaderBytes> header{};
    if (!recv_all(socket, header.data(), static_cast<int>(header.size()))) {
        return false;
    }

    if (header[0] != kMagic0 || header[1] != kMagic1) return false;
    if (static_cast<uint8_t>(header[2]) != kVersion) return false;

    auto type = static_cast<FrameType>(static_cast<uint8_t>(header[3]));
    uint32_t channel = get_u32(header.data() + 4);
    uint32_t len = get_u32(header.data() + 8);

    if (len > max_payload_bytes) return false;

    frame.type = type;
    frame.channel = channel;
    frame.payload.assign(len, 0);
    if (len > 0) {
        return recv_all(socket, reinterpret_cast<char*>(frame.payload.data()), static_cast<int>(len));
    }
    return true;
}

Frame hello_frame(const std::string& text) {
    Frame f;
    f.type = FrameType::hello;
    f.channel = 0;
    f.payload.assign(text.begin(), text.end());
    return f;
}

Frame data_frame(const char* bytes, int len) {
    Frame f;
    f.type = FrameType::data;
    f.channel = 1;
    f.payload.assign(bytes, bytes + len);
    return f;
}

Frame close_frame() {
    Frame f;
    f.type = FrameType::close;
    f.channel = 1;
    return f;
}

std::string payload_text(const Frame& frame) {
    return std::string(frame.payload.begin(), frame.payload.end());
}

} // namespace nbrelay::wire
