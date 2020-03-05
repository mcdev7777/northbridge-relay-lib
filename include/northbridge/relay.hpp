#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace nbrelay {

struct EdgeConfig {
    std::string bind_address = "127.0.0.1";
    uint16_t listen_port = 7080;

    std::string core_host = "127.0.0.1";
    uint16_t core_port = 7443;

    std::string access_token;
    uint32_t max_sessions = 64;
    uint32_t recv_buffer_bytes = 16 * 1024;
};

struct CoreConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t listen_port = 7443;

    std::string target_host = "127.0.0.1";
    uint16_t target_port = 5432;

    std::string access_token;
    uint32_t max_sessions = 128;
    uint32_t recv_buffer_bytes = 16 * 1024;
};

class EdgeRelay {
public:
    EdgeRelay();
    ~EdgeRelay();

    EdgeRelay(const EdgeRelay&) = delete;
    EdgeRelay& operator=(const EdgeRelay&) = delete;

    bool start(const EdgeConfig& config, std::string* error = nullptr);
    void stop();
    bool running() const;
    uint32_t active_sessions() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class CoreRelay {
public:
    CoreRelay();
    ~CoreRelay();

    CoreRelay(const CoreRelay&) = delete;
    CoreRelay& operator=(const CoreRelay&) = delete;

    bool start(const CoreConfig& config, std::string* error = nullptr);
    void stop();
    bool running() const;
    uint32_t active_sessions() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nbrelay
