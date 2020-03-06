#include <northbridge/relay.hpp>

#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>

namespace {
std::atomic_bool keep_running{true};

void on_signal(int) {
    keep_running.store(false);
}

uint16_t to_port(const char* text) {
    return static_cast<uint16_t>(std::stoi(text));
}

void print_usage() {
    std::cout << "relay_edge_host <bind> <listen-port> <core-host> <core-port> <token>\n";
}
}

int main(int argc, char** argv) {
    if (argc < 6) {
        print_usage();
        return 2;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    nbrelay::EdgeConfig cfg;
    cfg.bind_address = argv[1];
    cfg.listen_port = to_port(argv[2]);
    cfg.core_host = argv[3];
    cfg.core_port = to_port(argv[4]);
    cfg.access_token = argv[5];
    cfg.max_sessions = 96;

    nbrelay::EdgeRelay relay;
    std::string error;
    if (!relay.start(cfg, &error)) {
        std::cerr << "start failed: " << error << "\n";
        return 1;
    }

    std::cout << "edge relay listening on " << cfg.bind_address << ":" << cfg.listen_port << "\n";
    while (keep_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    relay.stop();
    return 0;
}
