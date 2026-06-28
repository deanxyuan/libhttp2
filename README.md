# libhttp2

**Version 1.0.1** | C++11 | Apache 2.0

A lightweight HTTP/2 protocol implementation library written in C++11. Provides HTTP/2 framing, HPACK header compression/decompression, stream management, flow control, and trailers support. Designed as a building block for HTTP/2 clients, servers, and gRPC transports.

## Features

- **HTTP/2 Framing** -- Full support for all HTTP/2 frame types: DATA, HEADERS, PRIORITY, RST_STREAM, SETTINGS, PUSH_PROMISE, PING, GOAWAY, WINDOW_UPDATE, and CONTINUATION (RFC 7540).
- **HPACK Header Compression** -- Static and dynamic table encoding/decoding, Huffman coding for header values, and configurable dynamic table size.
- **Stream Management** -- Complete stream state machine (idle, reserved, open, half-closed, closed) with per-stream priority tracking.
- **Flow Control** -- Connection-level and stream-level window-based flow control with a pluggable `FlowControlHandler` interface. Automatic flow control by default.
- **Trailers Support** -- Efficient handling of trailing headers (including grpc-status delivery) via `SendTrailingHeaders()` and `OnStreamHeaders` callback.
- **Connection Preface** -- Automatic client preface sending and server preface verification.
- **Zero External Dependencies** -- Self-contained C++11 library; no third-party dependencies required at build time.

## Supported Platforms

| Platform | Status |
|----------|--------|
| Linux | Supported |
| macOS | Supported |
| Windows | Supported |
| iOS | Supported |
| Android | Supported |

## Prerequisites

- CMake >= 3.10
- C++11 compliant compiler (GCC 4.8+, Clang 3.4+, MSVC 2015+)

## Build

### Static Library (default)

```bash
cd LibHttp2
mkdir build && cd build
cmake ..
cmake --build . --config RelWithDebInfo --parallel 4
```

This produces `libhttp2.a` (Unix) or `http2.lib` (Windows).

### Shared Library

```bash
cmake .. -DBUILD_SHARED_LIBS=ON
cmake --build . --config RelWithDebInfo --parallel 4
```

### Build without Tests

```bash
cmake .. -DLIBHTTP2_BUILD_TESTS=OFF
cmake --build . --config Release --parallel 4
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `LIBHTTP2_BUILD_TESTS` | `ON` | Build the test executables |
| `BUILD_SHARED_LIBS` | `OFF` | Build as shared library instead of static |
| `LIBHTTP2_ENABLE_ASAN` | `OFF` | Enable AddressSanitizer |
| `LIBHTTP2_ENABLE_UBSAN` | `OFF` | Enable UndefinedBehaviorSanitizer |
| `LIBHTTP2_ENABLE_TSAN` | `OFF` | Enable ThreadSanitizer |

## Quick Start

Below is a minimal example showing how to create a transport, implement the required interfaces, and process incoming HTTP/2 data.

```cpp
#include "http2/transport.h"
#include <cstdio>
#include <vector>

// Implement the output interface: libhttp2 calls this to send raw TCP data
class MySendService : public http2::SendService {
public:
    int SendRawData(uint64_t cid, const uint8_t *buf, uint32_t size) override {
        // Write `buf` of `size` bytes to the TCP connection identified by `cid`
        // Return >0 (bytes sent) on sync success, 0 if queued async, <0 on error
        return size; // placeholder: synchronous send
    }
};

// Implement the event handler: libhttp2 calls these when frames are decoded
class MyHandler : public http2::EventHandler {
public:
    void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
        auto& headers = stream->GetHeaders();
        for (auto& h : headers) {
            printf("Header: %s = %s\n", h.first.c_str(), h.second.c_str());
        }
    }

    void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
        uint32_t size = stream->DataSize();
        std::vector<uint8_t> buf(size);
        stream->ReadData(buf.data(), size);
        printf("Received %u bytes on stream %u\n", size, stream->StreamId());
    }

    void OnStreamClosed(std::shared_ptr<http2::Stream> stream, uint32_t error_code) override {
        printf("Stream %u closed, error=%u\n", stream->StreamId(), error_code);
    }
};

int main() {
    MySendService sender;
    MyHandler handler;

    // Create a transport for connection id 1, client-side
    auto transport = http2::CreateTransport(1, true, &sender);
    transport->SetEventHandler(&handler);

    // Send an HTTP/2 request: create a stream and send headers
    auto stream = transport->CreateStream();
    stream->SendHeaders({
        {":method",    "GET"},
        {":path",      "/"},
        {":scheme",    "https"},
        {":authority", "example.com"}
    }, true); // true = END_STREAM (no body)

    // When TCP data arrives from the network, feed it to the transport:
    // const uint8_t *tcp_data = ...;
    // uint32_t tcp_len = ...;
    // int consumed = transport->ReceivedData(tcp_data, tcp_len);
    // if (consumed < 0) { /* protocol error */ }
    // else { /* retain unconsumed bytes for next call */ }

    // Cleanup (automatic via unique_ptr)
    // transport is destroyed when it goes out of scope
    return 0;
}
```

### Server-Side Example

```cpp
MySendService sender;
MyHandler handler;

// Create a server-side transport (client_side = false)
auto transport = http2::CreateTransport(1, false, &sender);
transport->SetEventHandler(&handler);

// Feed the client's connection preface + frames:
// transport->ReceivedData(buf, len);

// When OnStreamHeaders fires, send a response:
void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
    // Send 200 OK response
    stream->SendHeaders({
        {":status", "200"}
    }, false); // false = more data follows

    // Send body
    const char *body = "Hello, World!";
    stream->SendData(reinterpret_cast<const uint8_t*>(body), strlen(body), true);
}
```

## API Overview

All public types reside in the `http2` namespace. The single public header is `http2/transport.h`.

### `SendService`

Output interface for sending raw TCP data. The library calls `SendRawData()` whenever HTTP/2 frames need to be transmitted. You must implement this to bridge the library to your network I/O layer.

```cpp
class SendService {
    virtual int SendRawData(uint64_t cid, const uint8_t *buf, uint32_t size) = 0;
};
```

Return values: `>0` = bytes sent synchronously, `0` = queued for async delivery, `<0` = error.

### `Stream`

Represents an HTTP/2 stream within a connection. Streams can both send outgoing frames and read incoming data/headers. Created via `Transport::CreateStream()` and delivered to `EventHandler` callbacks.

**Read-only state:**
- `ConnectionId()` -- connection identifier
- `StreamId()` -- HTTP/2 stream identifier
- `Flags()` -- frame flags from the current event (bitmask of `Http2FrameFlag`)
- `ErrorCode()` -- RST_STREAM error code (0 if no error)
- `CurrentState()` -- stream state as `int` (see `Http2StreamState` enum)
- `CurrentStateTyped()` -- stream state as `Http2StreamState` enum value

**Sending:**
- `SendHeaders(headers, end_stream)` -- send a HEADERS frame
- `SendData(data, size, end_stream)` -- send a DATA frame (auto-splits large payloads)
- `SendTrailingHeaders(headers)` -- send trailing headers with END_STREAM
- `SendRSTStream(error_code)` -- terminate the stream immediately

**Reading:**
- `DataSize()` -- bytes available in the data buffer
- `ReadData(buffer, size)` -- copy data out and consume it
- `PeekData(out_size)` -- zero-copy pointer to buffered data without consuming
- `GetHeaders()` -- decoded headers as `vector<pair<string, string>>`

### `EventHandler`

Callback interface for incoming HTTP/2 frame events. All callbacks are invoked synchronously from within `Transport::ReceivedData()`. Only `OnStreamHeaders` and `OnStreamData` are pure virtual; the rest have default empty implementations.

| Callback | When |
|----------|------|
| `OnStreamHeaders(stream)` | HEADERS or PUSH_PROMISE fully received (after CONTINUATION if needed) |
| `OnStreamData(stream)` | DATA frame received |
| `OnStreamClosed(stream, error_code)` | Stream transitions to Closed state |
| `OnSettings(cid, settings, ack)` | SETTINGS or SETTINGS ACK received |
| `OnPing(cid, data, ack)` | PING frame received |
| `OnGoAway(cid, last_stream_id, error_code, debug)` | GOAWAY received (peer shutting down) |
| `OnSendComplete(cid, success)` | Async send (SendRawData returned 0) completed |

### `FlowControlHandler`

Optional callback interface for custom HTTP/2 flow control. If not set, automatic flow control is used (WINDOW_UPDATE matches received data size). Implement this only if you need backpressure or custom window management (e.g., for proxies or gateways).

| Callback | Purpose |
|----------|---------|
| `OnDataReceived(cid, stream_id, recv_bytes)` | Called after data received; return `WindowUpdate` increments |
| `OnWindowUpdate(cid, stream_id, window_update_size)` | Called when WINDOW_UPDATE received from peer |
| `OnPreSendData(cid, stream_id, send_bytes)` | Called before sending data; return increments to allow |

### `WindowUpdate`

Output struct for flow control decisions used by `FlowControlHandler`.

```cpp
struct WindowUpdate {
    uint32_t connection_window_size_increment;  // bytes to add to connection window
    uint32_t stream_window_size_increment;      // bytes to add to stream window
};
```

### `ConnectionInfo`

Snapshot of connection-level state, returned by `Transport::GetConnectionInfo()`.

```cpp
struct ConnectionInfo {
    uint32_t active_streams;    // number of open/half-closed streams
    uint32_t last_stream_id;    // highest remote stream ID seen
    bool received_goaway;       // whether GOAWAY has been received
    bool sent_goaway;           // whether GOAWAY has been sent
    bool draining;              // whether the connection is in drain phase
    int64_t connection_window;  // current connection-level send window
};
```

### `Transport`

Core interface for an HTTP/2 connection. Each `Transport` instance manages one TCP connection. Created via `CreateTransport()` which returns a `std::unique_ptr<Transport>`.

**Handler setup:**
- `SetEventHandler(handler)` -- set the event callback handler (required)
- `SetFlowControlHandler(handler)` -- set custom flow control (optional)

**Connection info:**
- `GetConnectionId()` -- connection identifier
- `IsClientSide()` -- whether this is a client-side transport
- `GetRemoteSetting(id)` -- get a remote SETTINGS value by parameter ID
- `GetLocalSetting(id)` -- get a local SETTINGS value by parameter ID
- `GetConnectionInfo()` -- get a snapshot of connection state (returns `ConnectionInfo`)

**Stream management:**
- `CreateStream()` -- allocate a new stream (odd IDs for client, even for server)
- `FindStream(stream_id)` -- look up an existing stream by ID (returns `shared_ptr<Stream>` or nullptr)

**Sending frames:**
- `SendSettings(settings)` -- send a SETTINGS frame
- `SendPing(info)` -- send a PING frame
- `SendGoAway(error_code, last_stream_id, debug)` -- send a GOAWAY frame
- `SendPushPromise(request_stream, headers)` -- send a PUSH_PROMISE (server-side only, returns the promised Stream)
- `SendWindowUpdate(stream_id, increment)` -- send a WINDOW_UPDATE for connection or stream flow control

**Receiving data:**
- `ReceivedData(buf, len)` -- feed incoming TCP data; returns bytes consumed, or -1 on error

**Buffered sending:**
- `SetBufferedMode(enable)` -- enable/disable send buffering (reduces small TCP writes)
- `Flush()` -- send all accumulated buffered data
- `ScopedBufferedMode` -- RAII guard that enables buffered mode for a scope and auto-flushes on destruction

**Lifecycle:**
- `Shutdown()` -- send GOAWAY and clean up

### Factory Function

```cpp
std::unique_ptr<Transport> http2::CreateTransport(
    uint64_t connection_id,  // opaque id passed to callbacks
    bool client_side,        // true for client, false for server
    SendService *service     // output bridge (caller retains ownership)
);
```

### Enums

- `Http2ErrorCode` -- RST_STREAM/GOAWAY error codes (RFC 7540, Section 7)
- `Http2FrameType` -- frame type identifiers (DATA, HEADERS, SETTINGS, etc.)
- `Http2SettingsId` -- SETTINGS parameter identifiers
- `Http2StreamState` -- stream lifecycle states (idle, open, half-closed, closed)
- `Http2FrameFlag` -- frame flag bits (END_STREAM, ACK, END_HEADERS, etc.)

### Constants

- `http2::ALPN_PROTOCOL` -- `"h2"` (ALPN protocol identifier for HTTP/2 over TLS)
- `http2::ALPN_PROTOCOL_LEN` -- `2`

## TLS Integration

LibHttp2 is transport-agnostic: it operates on raw bytes and does not perform TLS itself. To use HTTP/2 over TLS, you must handle TLS negotiation externally and use ALPN to agree on the `"h2"` protocol (RFC 7540, Section 3.3).

The library provides ALPN constants for convenience:

```cpp
#include "http2/transport.h"

// http2::ALPN_PROTOCOL     -> "h2"
// http2::ALPN_PROTOCOL_LEN -> 2
```

### OpenSSL Example

```cpp
#include <openssl/ssl.h>
#include <openssl/alpn.h>
#include "http2/transport.h"

// Build the ALPN protocol list in the wire format expected by OpenSSL:
// each entry is prefixed by a single length byte.
unsigned char alpn_protos[] = {
    http2::ALPN_PROTOCOL_LEN,                          // length byte
    'h', '2',                                          // protocol string
};

// On the client side -- advertise h2 during the TLS handshake:
SSL_CTX_set_alpn_protos(ctx, alpn_protos, sizeof(alpn_protos));

// On the server side -- select h2 from the client's list:
// Use SSL_select_next_proto() in your ALPN callback registered with
// SSL_CTX_set_alpn_select_cb().
```

### h2c (Cleartext HTTP/2)

LibHttp2 supports h2c natively because it does not enforce TLS. For cleartext HTTP/2, the client sends the connection preface (magic string + SETTINGS frame) directly over TCP without any TLS wrapping. No special configuration is needed -- just feed the bytes to `Transport::ReceivedData()` as usual.

### Tracking Encryption State

Since the library is transport-agnostic, it has no concept of whether the underlying connection is encrypted. If your application needs to distinguish between encrypted and cleartext connections (e.g., for logging or security policy), track this externally in your own connection context:

```cpp
struct MyConnectionContext {
    uint64_t connection_id;
    bool is_encrypted;  // set based on your TLS setup
    // ...
};
```

## Testing

Tests are built by default. After building:

```bash
cd LibHttp2/build
ctest --output-on-failure
```

Individual test executables:

| Test | Description |
|------|-------------|
| `test_byte_order` | Byte order conversion utilities |
| `test_dynamic_table` | HPACK dynamic table operations |
| `test_hpack` | HPACK header encoding/decoding round-trip |
| `test_hpack_primitive` | HPACK wire-format decoding edge cases |
| `test_huffman` | HPACK Huffman coding |
| `test_integration` | End-to-end client/server round-trip |
| `test_integration_ext` | Extended integration (PING, SETTINGS, multi-stream) |
| `test_multi_stream_crash` | Multi-stream lifecycle regression test |
| `test_pack` | HTTP/2 frame packing |
| `test_parse` | HTTP/2 frame parsing |
| `test_protocol` | Protocol compliance (flow control, GOAWAY, CONTINUATION, drain) |
| `test_slice` | Zero-copy slice buffer |
| `test_static_table` | HPACK static table lookups |
| `test_util` | Utility classes (atomic, MPSC queue, murmur hash) |

The `test/pcap/` directory contains gRPC packet captures for reference during manual testing.

## Graceful Shutdown (Drain)

LibHttp2 supports two-phase graceful shutdown. `Drain()` sends a GOAWAY frame, stops accepting new streams, and continues processing in-flight streams until they complete:

```cpp
// Phase 1: send GOAWAY, stop accepting new streams, continue processing existing ones
transport->Drain();

// Phase 2: triggered automatically when all in-flight streams are closed
// EventHandler::OnShutdownComplete(cid) callback fires
```

| Operation | Normal | Draining |
|-----------|--------|----------|
| `CreateStream()` | Allowed | Returns `nullptr` |
| `ReceivedData()` | Processes normally | Continues (in-flight streams still active) |
| New HEADERS from peer | Creates new stream | Rejected (GOAWAY already sent) |
| All streams closed | — | `OnShutdownComplete` fires |

This enables graceful connection rotation in load balancers and long-lived gRPC connections.

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.

## Third-Party Code

This project includes utility code derived from [gRPC](https://github.com/grpc/grpc):

- `src/utils/mpscq.cc` / `mpscq.h` -- Multi-producer single-consumer queue (Copyright 2016 gRPC authors)
- `src/utils/atomic.h` -- Atomic wrapper with memory order abstraction (Copyright 2017 gRPC authors)
- `src/utils/slice.cc` / `slice.h` -- Zero-copy reference-counted buffer (derived from gRPC slice)
- `src/utils/murmur_hash.cc` / `murmur_hash.h` -- MurmurHash3 hash function (Copyright 2015 gRPC authors)

These files are used under the Apache License 2.0. See the copyright headers in each file for details.

This project also includes code derived from [nghttp2](https://github.com/nghttp2/nghttp2):

- `src/hpack/huffman.cc` / `huffman.h` / `huffman_data.cc` -- Huffman coding for HPACK (Copyright 2013 Tatsuhiro Tsujikawa, MIT License)

And from [LevelDB](https://github.com/google/leveldb):

- `src/utils/testutil.cc` / `testutil.h` -- Lightweight test framework (Copyright 2011 The LevelDB Authors, BSD 3-Clause License)
