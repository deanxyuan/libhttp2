/**
 * @file server.cc
 * @brief Minimal HTTP/2 server example using LibHttp2.
 *
 * Demonstrates:
 *   - Creating a server-side transport (is_client = false)
 *   - Handling incoming requests via EventHandler
 *   - Sending response HEADERS + DATA
 *   - Using Drain() for graceful shutdown
 *
 * Build:
 *   cmake --build . --target server
 *   ./server
 */

#include <cstdio>
#include <cstring>
#include <vector>

#include "http2/transport.h"

class LoopbackSendService : public http2::SendService {
public:
    http2::Transport *peer = nullptr;
    int SendRawData(uint64_t /*cid*/, const uint8_t *buf, uint32_t size) override {
        if (!peer) return -1;
        int consumed = peer->ReceivedData(buf, size);
        return consumed >= 0 ? static_cast<int>(size) : -1;
    }
};

class ServerHandler : public http2::EventHandler {
public:
    void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
        printf("[Server] Request on stream %u:\n", stream->StreamId());
        for (auto &h : stream->GetHeaders()) {
            printf("  %s: %s\n", h.first.c_str(), h.second.c_str());
        }
    }

    void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
        uint32_t size = stream->DataSize();
        std::vector<uint8_t> buf(size);
        stream->ReadData(buf.data(), size);
        printf("[Server] Received %u bytes on stream %u\n", size, stream->StreamId());

        // Respond
        stream->SendHeaders({{":status", "200"}}, false);
        const char *body = "Hello from server!";
        stream->SendData(reinterpret_cast<const uint8_t *>(body),
                         static_cast<uint32_t>(strlen(body)), true);
    }

    void OnStreamClosed(std::shared_ptr<http2::Stream> stream, uint32_t error_code) override {
        printf("[Server] Stream %u closed (error=%u)\n", stream->StreamId(), error_code);
    }
};

class ClientHandler : public http2::EventHandler {
public:
    void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
        printf("[Client] Response on stream %u:\n", stream->StreamId());
        for (auto &h : stream->GetHeaders()) {
            printf("  %s: %s\n", h.first.c_str(), h.second.c_str());
        }
    }

    void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
        uint32_t size = stream->DataSize();
        std::vector<uint8_t> buf(size);
        stream->ReadData(buf.data(), size);
        printf("[Client] Got %u bytes: %.*s\n",
               size, static_cast<int>(size),
               reinterpret_cast<const char *>(buf.data()));
    }

    void OnStreamClosed(std::shared_ptr<http2::Stream> stream, uint32_t error_code) override {
        printf("[Client] Stream %u closed (error=%u)\n", stream->StreamId(), error_code);
    }
};

int main() {
    printf("=== LibHttp2 Server Example ===\n\n");

    LoopbackSendService client_send, server_send;
    auto client = http2::CreateTransport(1, true, &client_send);
    auto server = http2::CreateTransport(2, false, &server_send);
    client_send.peer = server.get();
    server_send.peer = client.get();

    ClientHandler ch;
    ServerHandler sh;
    client->SetEventHandler(&ch);
    server->SetEventHandler(&sh);

    // Client sends a POST request with body
    printf("--- Sending request ---\n");
    auto stream = client->CreateStream();
    stream->SendHeaders({
        {":method", "POST"},
        {":path", "/hello"},
        {":scheme", "https"},
        {":authority", "example.com"},
    }, false);
    const char *body = "request body";
    stream->SendData(reinterpret_cast<const uint8_t *>(body),
                     static_cast<uint32_t>(strlen(body)), true);

    // Drain server for graceful shutdown
    printf("\n--- Draining server ---\n");
    server->Drain();

    printf("--- Done ---\n");
    return 0;
}
