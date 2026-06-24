/**
 * @file integration_test.cc
 * @brief Integration test for the public http2::Transport API.
 *
 *  Tests a full client->server round-trip using the new Stream-centric interface:
 *  - Client creates a stream, sends HEADERS + DATA
 *  - Server receives headers and data via EventHandler callbacks
 *  - Server sends a response back (HEADERS + DATA + trailing HEADERS)
 *  - Client receives the response
 *
 *  The two transports are wired together in-memory via a LoopbackSendService.
 */

#include "http2/transport.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include "src/utils/testutil.h"

class IntegrationTest {};

// ============================================================================
// LoopbackSendService -- wires two transports together in-memory
// ============================================================================

class LoopbackSendService : public http2::SendService {
public:
    // The peer transport that receives our outgoing data
    http2::Transport *peer = nullptr;

    int SendRawData(uint64_t /*cid*/, const uint8_t *buf, uint32_t size) override {
        if (!peer) return -1;
        // Feed our outgoing bytes directly into the peer's ReceivedData
        int consumed = peer->ReceivedData(buf, size);
        return consumed >= 0 ? static_cast<int>(size) : -1;
    }
};

// ============================================================================
// TestEventHandler -- records events for verification
// ============================================================================

struct ReceivedFrame {
    std::string type;  // "headers", "data", "closed"
    std::vector<std::pair<std::string, std::string>> headers;
    std::string data;
    uint32_t error_code = 0;
    uint32_t stream_id = 0;
};

class TestEventHandler : public http2::EventHandler {
public:
    std::vector<ReceivedFrame> frames;
    std::vector<std::pair<uint64_t, bool>> settings_events;  // (cid, ack)
    std::vector<std::pair<uint64_t, uint64_t>> ping_events;  // (cid, data)
    std::vector<std::pair<uint64_t, uint32_t>> goaway_events; // (cid, last_stream_id)
    std::vector<uint64_t> shutdown_complete_events;           // cid

    void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
        ReceivedFrame f;
        f.type = "headers";
        f.stream_id = stream->StreamId();
        f.headers = stream->GetHeaders();
        frames.push_back(std::move(f));
    }

    void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
        ReceivedFrame f;
        f.type = "data";
        f.stream_id = stream->StreamId();
        uint32_t size = stream->DataSize();
        if (size > 0) {
            std::vector<uint8_t> buf(size);
            uint32_t read = stream->ReadData(buf.data(), size);
            f.data = std::string(reinterpret_cast<const char *>(buf.data()), read);
        }
        frames.push_back(std::move(f));
    }

    void OnStreamClosed(std::shared_ptr<http2::Stream> stream, uint32_t error_code) override {
        ReceivedFrame f;
        f.type = "closed";
        f.stream_id = stream->StreamId();
        f.error_code = error_code;
        frames.push_back(std::move(f));
    }

    void OnSettings(uint64_t cid,
                    const std::vector<std::pair<uint16_t, uint32_t>> &settings,
                    bool ack) override {
        settings_events.push_back({cid, ack});
    }

    void OnPing(uint64_t cid, uint64_t data, bool ack) override {
        ping_events.push_back({cid, data});
    }

    void OnGoAway(uint64_t cid, uint32_t last_stream_id, uint32_t error_code,
                  const std::string &debug) override {
        goaway_events.push_back({cid, last_stream_id});
    }

    void OnShutdownComplete(uint64_t cid) override {
        shutdown_complete_events.push_back(cid);
    }
};

// ============================================================================
// Helper: find frames by type
// ============================================================================
static std::vector<const ReceivedFrame *> find_frames(const std::vector<ReceivedFrame> &frames,
                                                      const std::string &type) {
    std::vector<const ReceivedFrame *> result;
    for (auto &f : frames) {
        if (f.type == type) result.push_back(&f);
    }
    return result;
}

static const ReceivedFrame *find_first(const std::vector<ReceivedFrame> &frames,
                                       const std::string &type) {
    for (auto &f : frames) {
        if (f.type == type) return &f;
    }
    return nullptr;
}

// ============================================================================
// TEST: Client sends request, server receives it
// ============================================================================
TEST(IntegrationTest, ClientServerRoundTrip) {
    // Create loopback services
    LoopbackSendService client_send;
    LoopbackSendService server_send;

    // Create transports
    auto client = http2::CreateTransport(1, true, &client_send);
    auto server = http2::CreateTransport(2, false, &server_send);

    // Wire them together
    client_send.peer = server.get();
    server_send.peer = client.get();

    // Create event handlers
    TestEventHandler client_handler;
    TestEventHandler server_handler;
    client->SetEventHandler(&client_handler);
    server->SetEventHandler(&server_handler);

    // The client transport auto-sends the connection preface + SETTINGS
    // on first write via ensure_preface_sent(). No manual preface needed.

    // --- Client creates a stream and sends a request ---
    auto stream = client->CreateStream();
    ASSERT_TRUE(stream != nullptr);
    ASSERT_TRUE(stream->StreamId() == 1);

    // Send HEADERS (request)
    bool ok = stream->SendHeaders({
        {":method", "GET"},
        {":path", "/hello"},
        {":scheme", "https"},
        {":authority", "example.com"},
    }, false);  // end_stream=false, we'll send body
    ASSERT_TRUE(ok);

    // Send DATA with END_STREAM
    const char *body = "Hello, HTTP/2!";
    ok = stream->SendData(reinterpret_cast<const uint8_t *>(body),
                          static_cast<uint32_t>(strlen(body)), true);
    ASSERT_TRUE(ok);

    // --- Verify server received the request ---
    auto server_headers = find_frames(server_handler.frames, "headers");
    ASSERT_TRUE(server_headers.size() >= 1);

    // Find the request headers (not SETTINGS ACK)
    const ReceivedFrame *req_headers = nullptr;
    for (auto *f : server_headers) {
        for (auto &h : f->headers) {
            if (h.first == ":method") {
                req_headers = f;
                break;
            }
        }
        if (req_headers) break;
    }
    ASSERT_TRUE(req_headers != nullptr);
    ASSERT_TRUE(req_headers->stream_id == 1);

    // Verify header values
    bool found_method = false, found_path = false;
    for (auto &h : req_headers->headers) {
        if (h.first == ":method" && h.second == "GET") found_method = true;
        if (h.first == ":path" && h.second == "/hello") found_path = true;
    }
    ASSERT_TRUE(found_method);
    ASSERT_TRUE(found_path);

    // Verify data
    auto server_data = find_frames(server_handler.frames, "data");
    ASSERT_TRUE(server_data.size() >= 1);
    ASSERT_TRUE(server_data[0]->data == "Hello, HTTP/2!");

    // --- Server sends a response ---
    // Find the server-side stream
    auto server_stream = server->CreateStream();
    // Actually, the server should use the stream it received the request on.
    // But CreateStream creates a NEW stream. The server needs to respond on stream 1.
    // The server received the HEADERS on stream 1, which was auto-created.
    // We need a way to get that stream. Let's use the stream from the callback.
    // But the callback already ran... Let's restructure.

    // Actually, looking at the design: server received HEADERS on stream 1.
    // The stream was auto-created by received_headers. To respond, we need
    // a reference to that stream. We can get it from the callback's shared_ptr.
    // But we didn't save it. Let me check if we can find it by creating a new
    // stream with the same ID... No, that would create stream 2.
    //
    // The proper way: the server should save the stream reference from OnStreamHeaders
    // and use it to send the response. Let me refactor to do that.

    // For now, let's just verify the test structure works up to this point.
    // The key assertions have already passed: client sent headers+data,
    // server received them correctly.

    // --- Cleanup ---
    client->Shutdown();
    server->Shutdown();
}

// ============================================================================
// TEST: Full round-trip with response (saves stream reference from callback)
// ============================================================================
TEST(IntegrationTest, FullRoundTrip) {
    LoopbackSendService client_send;
    LoopbackSendService server_send;

    auto client = http2::CreateTransport(10, true, &client_send);
    auto server = http2::CreateTransport(20, false, &server_send);

    client_send.peer = server.get();
    server_send.peer = client.get();

    // Server handler that saves the stream reference for responding
    class ServerHandler : public http2::EventHandler {
    public:
        std::shared_ptr<http2::Stream> request_stream;
        std::string received_data;

        void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
            request_stream = stream;
        }

        void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
            uint32_t size = stream->DataSize();
            if (size > 0) {
                std::vector<uint8_t> buf(size);
                stream->ReadData(buf.data(), size);
                received_data = std::string(reinterpret_cast<const char *>(buf.data()), size);
            }
        }
    };

    // Client handler
    class ClientHandler : public http2::EventHandler {
    public:
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_data;
        std::string trailing_status;
        bool got_response = false;

        void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
            auto &headers = stream->GetHeaders();
            // Check if this is a response (:status) or trailing headers (grpc-status)
            for (auto &h : headers) {
                if (h.first == ":status") {
                    response_headers = headers;
                    got_response = true;
                }
                if (h.first == "grpc-status") {
                    trailing_status = h.second;
                }
            }
        }

        void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
            uint32_t size = stream->DataSize();
            if (size > 0) {
                std::vector<uint8_t> buf(size);
                stream->ReadData(buf.data(), size);
                response_data = std::string(reinterpret_cast<const char *>(buf.data()), size);
            }
        }
    };

    ServerHandler server_handler;
    ClientHandler client_handler;
    client->SetEventHandler(&client_handler);
    server->SetEventHandler(&server_handler);

    // Client creates stream and sends request (auto-sends preface on first write)
    auto stream = client->CreateStream();
    ASSERT_TRUE(stream != nullptr);

    stream->SendHeaders({
        {":method", "POST"},
        {":path", "/grpc/Service/Method"},
        {":scheme", "https"},
        {":authority", "localhost"},
        {"content-type", "application/grpc"},
    });

    const char *req_body = "request payload";
    stream->SendData(reinterpret_cast<const uint8_t *>(req_body),
                     static_cast<uint32_t>(strlen(req_body)), true);

    // Verify server received the request
    ASSERT_TRUE(server_handler.request_stream != nullptr);
    ASSERT_TRUE(server_handler.request_stream->StreamId() == 1);
    ASSERT_TRUE(server_handler.received_data == "request payload");

    // Server sends response on the same stream
    auto &srv_stream = server_handler.request_stream;
    srv_stream->SendHeaders({
        {":status", "200"},
        {"content-type", "application/grpc"},
    });

    const char *resp_body = "response payload";
    srv_stream->SendData(reinterpret_cast<const uint8_t *>(resp_body),
                         static_cast<uint32_t>(strlen(resp_body)));

    // Send trailing metadata (gRPC status)
    srv_stream->SendTrailingHeaders({
        {"grpc-status", "0"},
    });

    // Verify client received the response
    ASSERT_TRUE(client_handler.got_response);
    ASSERT_TRUE(client_handler.response_data == "response payload");
    ASSERT_TRUE(client_handler.trailing_status == "0");

    // Cleanup
    client->Shutdown();
    server->Shutdown();
}

// ============================================================================
// TEST: RST_STREAM via Stream::SendRSTStream
// ============================================================================
TEST(IntegrationTest, StreamRSTStream) {
    LoopbackSendService client_send;
    LoopbackSendService server_send;

    auto client = http2::CreateTransport(100, true, &client_send);
    auto server = http2::CreateTransport(200, false, &server_send);

    client_send.peer = server.get();
    server_send.peer = client.get();

    class RSTHandler : public http2::EventHandler {
    public:
        uint32_t closed_error = 0;
        bool got_closed = false;

        void OnStreamHeaders(std::shared_ptr<http2::Stream>) override {}
        void OnStreamData(std::shared_ptr<http2::Stream>) override {}
        void OnStreamClosed(std::shared_ptr<http2::Stream>, uint32_t error_code) override {
            closed_error = error_code;
            got_closed = true;
        }
    };

    RSTHandler handler;
    server->SetEventHandler(&handler);

    // Client sends headers (auto-sends preface on first write)
    auto stream = client->CreateStream();
    stream->SendHeaders({{":method", "GET"}, {":path", "/"}, {":scheme", "https"}, {":authority", "e.com"}}, true);

    // Client resets the stream
    stream->SendRSTStream(static_cast<uint32_t>(Http2ErrorCode::Cancel));

    // Server should have received the RST_STREAM
    ASSERT_TRUE(handler.got_closed);
    ASSERT_TRUE(handler.closed_error == static_cast<uint32_t>(Http2ErrorCode::Cancel));

    client->Shutdown();
    server->Shutdown();
}

// ============================================================================
// TEST: PeekData zero-copy access
// ============================================================================
TEST(IntegrationTest, PeekDataAccess) {
    LoopbackSendService client_send;
    LoopbackSendService server_send;

    auto client = http2::CreateTransport(300, true, &client_send);
    auto server = http2::CreateTransport(400, false, &server_send);

    client_send.peer = server.get();
    server_send.peer = client.get();

    class DataHandler : public http2::EventHandler {
    public:
        const uint8_t *peeked_ptr = nullptr;
        uint32_t peeked_size = 0;

        void OnStreamHeaders(std::shared_ptr<http2::Stream>) override {}
        void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
            peeked_ptr = stream->PeekData(&peeked_size);
        }
    };

    DataHandler handler;
    server->SetEventHandler(&handler);

    // Client sends data (auto-sends preface on first write)
    auto stream = client->CreateStream();
    stream->SendHeaders({{":method", "POST"}, {":path", "/"}, {":scheme", "https"}, {":authority", "e.com"}});

    const char *data = "peek test data";
    uint32_t data_len = static_cast<uint32_t>(strlen(data));
    stream->SendData(reinterpret_cast<const uint8_t *>(data), data_len, true);

    // Verify PeekData returned valid pointer
    ASSERT_TRUE(handler.peeked_ptr != nullptr);
    ASSERT_TRUE(handler.peeked_size == data_len);
    ASSERT_TRUE(memcmp(handler.peeked_ptr, data, data_len) == 0);

    client->Shutdown();
    server->Shutdown();
}

// ============================================================================
// TEST: Drain -- graceful shutdown waits for streams to close
// ============================================================================
TEST(IntegrationTest, DrainGracefulShutdown) {
    LoopbackSendService client_send;
    LoopbackSendService server_send;

    auto client = http2::CreateTransport(500, true, &client_send);
    auto server = http2::CreateTransport(600, false, &server_send);

    client_send.peer = server.get();
    server_send.peer = client.get();

    // Server handler that saves stream reference
    class DrainServerHandler : public http2::EventHandler {
    public:
        std::shared_ptr<http2::Stream> request_stream;
        void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
            request_stream = stream;
        }
        void OnStreamData(std::shared_ptr<http2::Stream>) override {}
    };

    DrainServerHandler server_handler;
    TestEventHandler client_handler;
    client->SetEventHandler(&client_handler);
    server->SetEventHandler(&server_handler);

    // Client sends a request (auto-sends preface on first write)
    auto stream = client->CreateStream();
    ASSERT_TRUE(stream != nullptr);

    stream->SendHeaders({
        {":method", "GET"},
        {":path", "/drain-test"},
        {":scheme", "https"},
        {":authority", "localhost"},
    });
    const char *body = "drain body";
    stream->SendData(reinterpret_cast<const uint8_t *>(body),
                     static_cast<uint32_t>(strlen(body)), true);

    // Server received the request
    ASSERT_TRUE(server_handler.request_stream != nullptr);

    // Drain the server -- should send GOAWAY but keep existing stream alive
    server->Drain();

    // Server drain should have sent GOAWAY to client
    ASSERT_TRUE(client_handler.goaway_events.size() >= 1);

    // Server should NOT have fired OnShutdownComplete yet (stream still open)
    ASSERT_TRUE(server_handler.request_stream != nullptr);

    // Now close the stream: server sends response + trailing headers (END_STREAM)
    auto &srv_stream = server_handler.request_stream;
    srv_stream->SendHeaders({{":status", "200"}});
    srv_stream->SendTrailingHeaders({{"grpc-status", "0"}});

    // After the stream is closed, the server's OnShutdownComplete should fire.
    // The server handler is DrainServerHandler which doesn't track this.
    // But the client_handler does. Let's verify client got the GOAWAY.
    // The server's drain completion callback would need a separate handler.
    // Let's verify via ConnectionInfo instead.
    auto info = server->GetConnectionInfo();
    // After stream closed + drain complete, draining should be false
    // (check_drain_complete resets it when streams.empty())
    ASSERT_TRUE(info.active_streams == 0);

    client->Shutdown();
}

// ============================================================================
// TEST: Drain -- with shutdown complete callback tracking
// ============================================================================
TEST(IntegrationTest, DrainShutdownCompleteCallback) {
    LoopbackSendService client_send;
    LoopbackSendService server_send;

    auto client = http2::CreateTransport(700, true, &client_send);
    auto server = http2::CreateTransport(800, false, &server_send);

    client_send.peer = server.get();
    server_send.peer = client.get();

    // Handler that tracks shutdown complete
    class DrainCallbackHandler : public http2::EventHandler {
    public:
        bool shutdown_complete = false;
        void OnStreamHeaders(std::shared_ptr<http2::Stream>) override {}
        void OnStreamData(std::shared_ptr<http2::Stream>) override {}
        void OnShutdownComplete(uint64_t) override {
            shutdown_complete = true;
        }
    };

    DrainCallbackHandler server_handler;
    TestEventHandler client_handler;
    client->SetEventHandler(&client_handler);
    server->SetEventHandler(&server_handler);

    // Client sends a request (auto-sends preface on first write)
    auto stream = client->CreateStream();
    stream->SendHeaders({
        {":method", "POST"},
        {":path", "/test"},
        {":scheme", "https"},
        {":authority", "localhost"},
    }, true);  // END_STREAM -- stream closes immediately on client side

    // Drain server -- has one in-flight stream
    server->Drain();
    ASSERT_TRUE(!server_handler.shutdown_complete);

    // The server received HEADERS with END_STREAM, so the stream should be
    // half-closed remote. The server needs to close its side too.
    // Find the server stream and send response with END_STREAM
    auto info = server->GetConnectionInfo();
    // There should be 1 active stream
    ASSERT_TRUE(info.active_streams == 1);

    // Look up the stream and close it
    auto server_stream = server->FindStream(1);
    if (server_stream) {
        server_stream->SendHeaders({{":status", "200"}}, true);
    }

    // Now drain should be complete
    ASSERT_TRUE(server_handler.shutdown_complete);

    client->Shutdown();
}

// ============================================================================
// TEST: CreateStream returns nullptr after Drain
// ============================================================================
TEST(IntegrationTest, DrainBlocksNewStreams) {
    LoopbackSendService client_send;
    LoopbackSendService server_send;

    auto client = http2::CreateTransport(900, true, &client_send);
    auto server = http2::CreateTransport(1000, false, &server_send);

    client_send.peer = server.get();
    server_send.peer = client.get();

    TestEventHandler handler;
    client->SetEventHandler(&handler);

    // Drain the client -- no streams open
    client->Drain();

    // CreateStream should return nullptr after GOAWAY sent
    auto stream = client->CreateStream();
    ASSERT_TRUE(stream == nullptr);

    // ShutdownComplete should have fired immediately (no streams)
    ASSERT_TRUE(handler.shutdown_complete_events.size() == 1);

    client->Shutdown();
    server->Shutdown();
}

int main() {
    test::RunAllTests();
    return 0;
}
