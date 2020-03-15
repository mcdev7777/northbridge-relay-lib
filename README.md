# NorthBridge Relay Library

NorthBridge is a small Windows C++ library for routing private TCP service traffic through a controlled relay pair. It was written for situations where a workstation or service should expose one local TCP port while the real target service stays behind a separate relay boundary.

The transport uses a compact internal frame format called `NB/1`. It is intentionally small: a handshake frame, data frames, close frames, and a light channel field reserved for later multiplexing.

## What problem it solves

A common internal infrastructure issue is that a service is reachable from one Windows host but not directly reachable from another environment because of firewall, routing, or segmentation rules. NorthBridge lets an application open a local listener, forward client bytes through a framed relay connection, and land those bytes on a specific target service.

Typical use cases:

- Controlled access to a private database or internal API from a Windows operations workstation
- Routing traffic between two network zones without changing client software
- Embedding a small relay layer inside an existing Windows service
- Exercising custom TCP framing, connection lifecycle handling, and multithreaded socket code

## Architecture

```
client app -> EdgeRelay local listener -> NB/1 framed link -> CoreRelay -> target service
```

### EdgeRelay

`EdgeRelay` listens on a local address and port. Every accepted client connection creates a new worker session. The session connects to the core relay, performs an `NB/1` handshake, then moves bytes in both directions using two worker threads.

### CoreRelay

`CoreRelay` accepts framed `NB/1` sessions from edge relays. After token validation it opens a TCP connection to the configured target service and bridges traffic between the framed edge connection and the raw target socket.

## Frame format

Each frame has a 12-byte header:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Magic bytes: `N`, `B` |
| 2 | 1 | Version: `1` |
| 3 | 1 | Type: hello, data, close, ping |
| 4 | 4 | Channel id, network byte order |
| 8 | 4 | Payload length, network byte order |

Payload follows the header. The current implementation uses channel `1` for data traffic and channel `0` for control handshake traffic.

## Public API

```cpp
#include <northbridge/relay.hpp>

nbrelay::CoreConfig core;
core.bind_address = "0.0.0.0";
core.listen_port = 7443;
core.target_host = "10.10.20.15";
core.target_port = 5432;
core.access_token = "change-this-token";

nbrelay::CoreRelay core_relay;
std::string error;
if (!core_relay.start(core, &error)) {
    // log error
}
```

```cpp
#include <northbridge/relay.hpp>

nbrelay::EdgeConfig edge;
edge.bind_address = "127.0.0.1";
edge.listen_port = 15432;
edge.core_host = "relay.company.net";
edge.core_port = 7443;
edge.access_token = "change-this-token";

nbrelay::EdgeRelay edge_relay;
std::string error;
if (!edge_relay.start(edge, &error)) {
    // log error
}
```

## Build on Windows

Requirements:

- Visual Studio 2022 Build Tools
- CMake 3.18+
- Windows SDK with Winsock2 headers

Build commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The build produces:

- `northbridge_relay.lib`
- `relay_edge_host.exe`
- `relay_core_host.exe`

## Running the relay hosts

Start the core side near the target service:

```powershell
relay_core_host.exe 0.0.0.0 7443 127.0.0.1 5432 local-dev-token
```

Start the edge side on the client machine:

```powershell
relay_edge_host.exe 127.0.0.1 15432 192.168.10.40 7443 local-dev-token
```

Applications can then connect to `127.0.0.1:15432`; the relay forwards traffic to `127.0.0.1:5432` from the core side.

## Notes for integration

- The library owns Winsock startup and cleanup internally.
- `start()` returns immediately after the accept loop is launched.
- `stop()` closes the listener and joins the accept thread.
- Worker sessions are intentionally independent; one failed client does not stop the listener.
- `active_sessions()` can be used by a service health endpoint or status log.

## Current limits

- It uses one framed TCP connection per client session, not channel multiplexing yet.
- It uses per-session threads instead of IOCP. That is easier to audit and acceptable for moderate concurrency.
- The token handshake is only an application-level guard. Put this behind TLS, VPN, or a trusted network boundary for real environments.
- There is no persistent reconnect queue. Failed sessions fail fast and the caller reconnects normally.

## Suggested next steps

- Add TLS support through Schannel or an existing approved TLS layer.
- Add an IOCP backend for high connection counts.
- Add structured logging hooks instead of `OutputDebugStringA`.
- Add traffic counters per session.
- Add a small integration test harness around loopback services.
