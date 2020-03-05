#include <northbridge/relay.hpp>

#include "socket_util.hpp"
#include "wire.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace nbrelay {

namespace {

void log_line(const std::string& line) {
    ::OutputDebugStringA(("[northbridge] " + line + "\n").c_str());
}

void copy_raw_to_frame(SOCKET raw, SOCKET framed, std::atomic_bool& alive, uint32_t buffer_size) {
    std::vector<char> buffer(buffer_size == 0 ? 16384 : buffer_size);

    while (alive.load()) {
        int n = ::recv(raw, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (n <= 0) break;

        if (!wire::write_frame(framed, wire::data_frame(buffer.data(), n))) {
            break;
        }
    }

    wire::write_frame(framed, wire::close_frame());
    ::shutdown(raw, SD_BOTH);
    ::shutdown(framed, SD_SEND);
    alive.store(false);
}

void copy_frame_to_raw(SOCKET framed, SOCKET raw, std::atomic_bool& alive, uint32_t buffer_limit) {
    wire::Frame frame;

    while (alive.load()) {
        if (!wire::read_frame(framed, frame, buffer_limit == 0 ? 16384 : buffer_limit)) {
            break;
        }

        if (frame.type == wire::FrameType::close) {
            break;
        }

        if (frame.type != wire::FrameType::data) {
            // Older internal callers sometimes send ping frames through this path.
            // We ignore them rather than making the session fail hard.
            continue;
        }

        if (!frame.payload.empty()) {
            if (!wire::send_all(raw,
                                reinterpret_cast<const char*>(frame.payload.data()),
                                static_cast<int>(frame.payload.size()))) {
                break;
            }
        }
    }

    ::shutdown(raw, SD_BOTH);
    alive.store(false);
}

void copy_raw_to_raw(SOCKET left, SOCKET right, std::atomic_bool& alive, uint32_t buffer_size) {
    std::vector<char> buffer(buffer_size == 0 ? 16384 : buffer_size);

    while (alive.load()) {
        int n = ::recv(left, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (n <= 0) break;

        if (!wire::send_all(right, buffer.data(), n)) {
            break;
        }
    }

    alive.store(false);
}

} // namespace

struct EdgeRelay::Impl {
    EdgeConfig config;
    std::unique_ptr<net::WinsockRuntime> winsock;
    SOCKET listener = INVALID_SOCKET;
    std::thread accept_thread;
    std::atomic_bool is_running{false};
    std::atomic_uint32_t active{0};

    bool start(const EdgeConfig& cfg, std::string* error) {
        if (is_running.exchange(true)) {
            if (error) *error = "edge relay is already running";
            return false;
        }

        config = cfg;
        winsock = std::make_unique<net::WinsockRuntime>();
        if (!winsock->ok()) {
            is_running.store(false);
            if (error) *error = "WSAStartup failed";
            return false;
        }

        listener = net::make_listener(config.bind_address, config.listen_port, error);
        if (listener == INVALID_SOCKET) {
            is_running.store(false);
            return false;
        }

        accept_thread = std::thread([this]() { accept_loop(); });
        return true;
    }

    void stop() {
        if (!is_running.exchange(false)) return;
        net::close_socket(listener);
        listener = INVALID_SOCKET;

        if (accept_thread.joinable()) {
            accept_thread.join();
        }
    }

    void accept_loop() {
        log_line("edge listener started");

        while (is_running.load()) {
            sockaddr_in addr{};
            int addr_len = sizeof(addr);
            SOCKET client = ::accept(listener, reinterpret_cast<sockaddr*>(&addr), &addr_len);
            if (client == INVALID_SOCKET) {
                if (is_running.load()) log_line("accept failed at edge");
                continue;
            }

            if (active.load() >= config.max_sessions) {
                net::close_socket(client);
                continue;
            }

            net::set_nodelay(client);
            std::thread([this, client]() { run_client_session(client); }).detach();
        }
    }

    void run_client_session(SOCKET client) {
        active.fetch_add(1);
        SOCKET core = INVALID_SOCKET;

        do {
            std::string err;
            core = net::connect_tcp(config.core_host, config.core_port, &err);
            if (core == INVALID_SOCKET) {
                log_line(err);
                break;
            }

            std::string hello = "NB/1 " + config.access_token;
            if (!wire::write_frame(core, wire::hello_frame(hello))) break;

            wire::Frame reply;
            if (!wire::read_frame(core, reply, 1024)) break;
            if (reply.type != wire::FrameType::hello || wire::payload_text(reply) != "OK") {
                log_line("core rejected edge session");
                break;
            }

            std::atomic_bool alive{true};
            std::thread up([&]() {
                copy_raw_to_frame(client, core, alive, config.recv_buffer_bytes);
            });
            std::thread down([&]() {
                copy_frame_to_raw(core, client, alive, config.recv_buffer_bytes);
            });

            up.join();
            down.join();
        } while (false);

        net::close_socket(core);
        net::close_socket(client);
        active.fetch_sub(1);
    }
};

struct CoreRelay::Impl {
    CoreConfig config;
    std::unique_ptr<net::WinsockRuntime> winsock;
    SOCKET listener = INVALID_SOCKET;
    std::thread accept_thread;
    std::atomic_bool is_running{false};
    std::atomic_uint32_t active{0};

    bool start(const CoreConfig& cfg, std::string* error) {
        if (is_running.exchange(true)) {
            if (error) *error = "core relay is already running";
            return false;
        }

        config = cfg;
        winsock = std::make_unique<net::WinsockRuntime>();
        if (!winsock->ok()) {
            is_running.store(false);
            if (error) *error = "WSAStartup failed";
            return false;
        }

        listener = net::make_listener(config.bind_address, config.listen_port, error);
        if (listener == INVALID_SOCKET) {
            is_running.store(false);
            return false;
        }

        accept_thread = std::thread([this]() { accept_loop(); });
        return true;
    }

    void stop() {
        if (!is_running.exchange(false)) return;
        net::close_socket(listener);
        listener = INVALID_SOCKET;

        if (accept_thread.joinable()) {
            accept_thread.join();
        }
    }

    void accept_loop() {
        log_line("core listener started");

        while (is_running.load()) {
            sockaddr_in addr{};
            int addr_len = sizeof(addr);
            SOCKET edge = ::accept(listener, reinterpret_cast<sockaddr*>(&addr), &addr_len);
            if (edge == INVALID_SOCKET) {
                if (is_running.load()) log_line("accept failed at core");
                continue;
            }

            if (active.load() >= config.max_sessions) {
                net::close_socket(edge);
                continue;
            }

            net::set_nodelay(edge);
            std::thread([this, edge]() { run_edge_session(edge); }).detach();
        }
    }

    bool validate_edge(SOCKET edge) {
        wire::Frame hello;
        if (!wire::read_frame(edge, hello, 4096)) return false;
        if (hello.type != wire::FrameType::hello) return false;

        std::string expected = "NB/1 " + config.access_token;
        if (wire::payload_text(hello) != expected) {
            wire::write_frame(edge, wire::hello_frame("NO"));
            return false;
        }

        return wire::write_frame(edge, wire::hello_frame("OK"));
    }

    void run_edge_session(SOCKET edge) {
        active.fetch_add(1);
        SOCKET target = INVALID_SOCKET;

        do {
            if (!validate_edge(edge)) {
                log_line("edge validation failed");
                break;
            }

            std::string err;
            target = net::connect_tcp(config.target_host, config.target_port, &err);
            if (target == INVALID_SOCKET) {
                log_line(err);
                wire::write_frame(edge, wire::close_frame());
                break;
            }

            std::atomic_bool alive{true};
            std::thread in([&]() {
                copy_frame_to_raw(edge, target, alive, config.recv_buffer_bytes);
            });
            std::thread out([&]() {
                copy_raw_to_frame(target, edge, alive, config.recv_buffer_bytes);
            });

            in.join();
            out.join();
        } while (false);

        net::close_socket(target);
        net::close_socket(edge);
        active.fetch_sub(1);
    }
};

EdgeRelay::EdgeRelay() : impl_(std::make_unique<Impl>()) {}
EdgeRelay::~EdgeRelay() { stop(); }

bool EdgeRelay::start(const EdgeConfig& config, std::string* error) {
    return impl_->start(config, error);
}

void EdgeRelay::stop() { impl_->stop(); }
bool EdgeRelay::running() const { return impl_->is_running.load(); }
uint32_t EdgeRelay::active_sessions() const { return impl_->active.load(); }

CoreRelay::CoreRelay() : impl_(std::make_unique<Impl>()) {}
CoreRelay::~CoreRelay() { stop(); }

bool CoreRelay::start(const CoreConfig& config, std::string* error) {
    return impl_->start(config, error);
}

void CoreRelay::stop() { impl_->stop(); }
bool CoreRelay::running() const { return impl_->is_running.load(); }
uint32_t CoreRelay::active_sessions() const { return impl_->active.load(); }

} // namespace nbrelay
