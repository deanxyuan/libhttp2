/**
 * @file client.cc
 * @brief Minimal HTTP/2 client example using LibHttp2.
 *
 * Demonstrates:
 *   - Implementing SendService (output bridge)
 *   - Implementing EventHandler (incoming frame callbacks)
 *   - Creating a client-side transport
 *   - Sending HEADERS + DATA on a stream
 *   - Receiving a response
 *
 * This example uses an in-memory loopback to make it self-contained.
 * In a real application, SendService::SendRawData() would write to a TCP socket,
 * and ReceivedData() would be called when TCP data arrives from the network.
 *
 * Build:
 *   cmake --build . --target client_example
 *   ./client_example
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "http2/transport.h"

// ============================================================================
// SendService -- bridge from libhttp2 to your network I/O layer
// ============================================================================

// In this example, we use a loopback that feeds outgoing bytes directly
// into the peer transport's ReceivedData(). In production, write to TCP.
class LoopbackSendService : public http2::SendService {
public:
    http2::Transport *peer = nullptr;

    int SendRawData(uint64_t /*cid*/, const uint8_t *buf, uint32_t size) override {
        if (!peer) return -1;
        int consumed = peer->ReceivedData(buf, size);
        return consumed >= 0 ? static_cast<int>(size) : -1;
    }
};

// ============================================================================
// EventHandler -- callbacks for incoming HTTP/2 events
// ============================================================================

class ClientHandler : public http2::EventHandler {
public:
    void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
        printf("[Client] Stream %u received response headers:\n", stream->StreamId());
        for (auto &h : stream->GetHeaders()) {
            printf("  %s: %s\n", h.first.c_str(), h.second.c_str());
        }
    }

    void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
        uint32_t size = stream->DataSize();
        std::vector<uint8_t> buf(size);
        stream->ReadData(buf.data(), size);
        printf("[Client] Stream %u received %u bytes: %.*s\n",
               stream->StreamId(), size, static_cast<int>(size),
               reinterpret_cast<const char *>(buf.data()));
    }

    void OnStreamClosed(std::shared_ptr<http2::Stream> stream, uint32_t error_code) override {
        printf("[Client] Stream %u closed (error=%u)\n", stream->StreamId(), error_code);
    }
};

// ============================================================================
// Server-side handler -- responds to incoming requests
// ============================================================================

class ServerHandler : public http2::EventHandler {
public:
    void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
        printf("[Server] Stream %u received request headers:\n", stream->StreamId());
        for (auto &h : stream->GetHeaders()) {
            printf("  %s: %s\n", h.first.c_str(), h.second.c_str());
        }
    }

    void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
        uint32_t size = stream->DataSize();
        std::vector<uint8_t> buf(size);
        stream->ReadData(buf.data(), size);
        printf("[Server] Stream %u received %u bytes of body\n", stream->StreamId(), size);

        // Send a response
        stream->SendHeaders({{":status", "200"}}, false);
        const char *body = "Hello from LibHttp2 server!";
        stream->SendData(reinterpret_cast<const uint8_t *>(body), static_cast<uint32_t>(strlen(body)), true);
    }

    // If the request has END_STREAM on HEADERS (no body), respond here
    void OnStreamClosed(std::shared_ptr<http2::Stream> stream, uint32_t error_code) override {
        printf("[Server] Stream %u closed (error=%u)\n", stream->StreamId(), error_code);
    }
};

// ============================================================================
// Main -- wire up client and server, send a request, receive the response
// ============================================================================

int main() {
    printf("=== LibHttp2 Client Example ===\n\n");

    // Create loopback services
    LoopbackSendService client_send;
    LoopbackSendService server_send;

    // Create transports: (connection_id, is_client, send_service)
    auto client = http2::CreateTransport(1, true, &client_send);
    auto server = http2::CreateTransport(2, false, &server_send);

    // Wire the loopback: each transport's output feeds into the other's input
    client_send.peer = server.get();
    server_send.peer = client.get();

    // Set event handlers
    ClientHandler client_handler;
    ServerHandler server_handler;
    client->SetEventHandler(&client_handler);
    server->SetEventHandler(&server_handler);

    // Client sends the HTTP/2 connection preface (happens automatically on first use)
    // Client creates a stream and sends a request
    auto stream = client->CreateStream();
    printf("[Client] Sending request on stream %u\n", stream->StreamId());

    stream->SendHeaders({
        {":method",    "POST"},
        {":path",      "/api/hello"},
        {":scheme",    "https"},
        {":authority", "example.com"},
        {"content-type", "application/json"},
    }, false); // false = more data follows

    const char *body = R"({"name": "LibHttp2"})";
    stream->SendData(reinterpret_cast<const uint8_t *>(body), static_cast<uint32_t>(strlen(body)), true);
    // true = END_STREAM (request complete)

    printf("\n--- Done ---\n");
    return 0;
}
