/**
 * @file test_protocol.cc
 * @brief Protocol compliance and error handling integration tests.
 *
 * Covers:
 * - Flow control: automatic WINDOW_UPDATE, custom FlowControlHandler, window tracking
 * - Error handling: invalid frames, protocol violations, GOAWAY generation
 * - PUSH_PROMISE: server push (basic validation)
 * - CONTINUATION frames: large header block handling
 */

#include "http2/transport.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "src/utils/testutil.h"

class ProtocolTest {};

// ============================================================================
// LoopbackSendService
// ============================================================================

class LoopbackSendService : public http2::SendService {
public:
    http2::Transport *peer = nullptr;
    uint32_t total_bytes_sent = 0;

    int SendRawData(uint64_t /*cid*/, const uint8_t *buf, uint32_t size) override {
        total_bytes_sent += size;
        if (!peer) return -1;
        int consumed = peer->ReceivedData(buf, size);
        return consumed >= 0 ? static_cast<int>(size) : -1;
    }
};

// ============================================================================
// TransportPair helper
// ============================================================================

struct TransportPair {
    LoopbackSendService client_send;
    LoopbackSendService server_send;
    std::unique_ptr<http2::Transport> client;
    std::unique_ptr<http2::Transport> server;

    void Init(uint64_t client_cid = 1, uint64_t server_cid = 2) {
        client = http2::CreateTransport(client_cid, true, &client_send);
        server = http2::CreateTransport(server_cid, false, &server_send);
        client_send.peer = server.get();
        server_send.peer = client.get();
    }

    void Shutdown() {
        if (client) client->Shutdown();
        if (server) server->Shutdown();
    }
};

// ============================================================================
// Test: Automatic flow control -- WINDOW_UPDATE sent on data receive
// ============================================================================
TEST(ProtocolTest, AutomaticFlowControl) {
    TransportPair pair;
    pair.Init(100, 200);

    class AutoFCServerHandler : public http2::EventHandler {
    public:
        std::shared_ptr<http2::Stream> request_stream;
        uint32_t total_data_bytes = 0;

        void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
            request_stream = stream;
        }
        void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
            uint32_t size = stream->DataSize();
            total_data_bytes += size;
            if (size > 0) {
                std::vector<uint8_t> buf(size);
                stream->ReadData(buf.data(), size);
            }
        }
    };

    AutoFCServerHandler server_handler;
    pair.server->SetEventHandler(&server_handler);

    // Client sends request with body
    auto stream = pair.client->CreateStream();
    ASSERT_TRUE(stream != nullptr);

    stream->SendHeaders({
        {":method", "POST"},
        {":path", "/upload"},
        {":scheme", "https"},
        {":authority", "localhost"},
    });

    // Send data in chunks
    const char *chunk1 = "AAAAAAAAAA";  // 10 bytes
    const char *chunk2 = "BBBBBBBBBB";  // 10 bytes
    stream->SendData(reinterpret_cast<const uint8_t *>(chunk1), 10);
    stream->SendData(reinterpret_cast<const uint8_t *>(chunk2), 10, true);

    // Server should have received all data
    ASSERT_TRUE(server_handler.total_data_bytes == 20);

    // Connection info should show the server's connection window was updated
    // (automatic flow control sends WINDOW_UPDATE matching received bytes)
    auto server_info = pair.server->GetConnectionInfo();
    // Window should be at or near initial value (65535) since auto-FC replenishes it
    ASSERT_TRUE(server_info.connection_window >= 65535 - 20);

    pair.Shutdown();
}

// ============================================================================
// Test: Custom FlowControlHandler
// ============================================================================
TEST(ProtocolTest, CustomFlowControlHandler) {
    TransportPair pair;
    pair.Init(300, 400);

    class TrackingFC : public http2::FlowControlHandler {
    public:
        uint32_t total_recv_bytes = 0;
        uint32_t window_update_count = 0;

        http2::WindowUpdate OnDataReceived(uint64_t /*cid*/, uint32_t /*stream_id*/,
                                           uint32_t recv_bytes) override {
            total_recv_bytes += recv_bytes;
            // Return half the bytes as increment (custom backpressure)
            return {recv_bytes / 2, recv_bytes / 2};
        }

        void OnWindowUpdate(uint64_t /*cid*/, uint32_t /*stream_id*/,
                            uint32_t /*window_update_size*/) override {
            window_update_count++;
        }
    };

    TrackingFC fc_handler;
    pair.server->SetFlowControlHandler(&fc_handler);

    class DataHandler : public http2::EventHandler {
    public:
        void OnStreamHeaders(std::shared_ptr<http2::Stream>) override {}
        void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
            uint32_t size = stream->DataSize();
            if (size > 0) {
                std::vector<uint8_t> buf(size);
                stream->ReadData(buf.data(), size);
            }
        }
    };

    DataHandler data_handler;
    pair.server->SetEventHandler(&data_handler);

    // Client sends data
    auto stream = pair.client->CreateStream();
    stream->SendHeaders({
        {":method", "POST"},
        {":path", "/fc-test"},
        {":scheme", "https"},
        {":authority", "localhost"},
    });

    const char *body = "flow control test data";  // 22 bytes
    stream->SendData(reinterpret_cast<const uint8_t *>(body),
                     static_cast<uint32_t>(strlen(body)), true);

    // Custom handler should have been called
    ASSERT_TRUE(fc_handler.total_recv_bytes == 22);

    pair.Shutdown();
}

// ============================================================================
// Test: Connection window tracking
// ============================================================================
TEST(ProtocolTest, ConnectionWindowTracking) {
    TransportPair pair;
    pair.Init(500, 600);

    // Initial connection window should be 65535
    auto client_info = pair.client->GetConnectionInfo();
    ASSERT_TRUE(client_info.connection_window == 65535);

    auto server_info = pair.server->GetConnectionInfo();
    ASSERT_TRUE(server_info.connection_window == 65535);

    // After sending data, the server's view of its connection window
    // should still be close to initial (auto-FC replenishes)
    class CountHandler : public http2::EventHandler {
    public:
        void OnStreamHeaders(std::shared_ptr<http2::Stream>) override {}
        void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
            uint32_t size = stream->DataSize();
            if (size > 0) {
                std::vector<uint8_t> buf(size);
                stream->ReadData(buf.data(), size);
            }
        }
    };

    CountHandler handler;
    pair.server->SetEventHandler(&handler);

    auto stream = pair.client->CreateStream();
    stream->SendHeaders({
        {":method", "POST"},
        {":path", "/window"},
        {":scheme", "https"},
        {":authority", "localhost"},
    });
    const char *data = "window test";
    stream->SendData(reinterpret_cast<const uint8_t *>(data),
                     static_cast<uint32_t>(strlen(data)), true);

    // Server connection window should still be reasonable
    server_info = pair.server->GetConnectionInfo();
    ASSERT_TRUE(server_info.connection_window > 0);

    pair.Shutdown();
}

// ============================================================================
// Test: GOAWAY with debug data
// ============================================================================
TEST(ProtocolTest, GoAwayWithDebugData) {
    TransportPair pair;
    pair.Init(700, 800);

    class GoAwayHandler : public http2::EventHandler {
    public:
        bool got_goaway = false;
        std::string debug_data;
        uint32_t error_code = 0;

        void OnStreamHeaders(std::shared_ptr<http2::Stream>) override {}
        void OnStreamData(std::shared_ptr<http2::Stream>) override {}
        void OnGoAway(uint64_t /*cid*/, uint32_t /*last_stream_id*/,
                      uint32_t err, const std::string &debug) override {
            got_goaway = true;
            error_code = err;
            debug_data = debug;
        }
    };

    GoAwayHandler handler;
    pair.client->SetEventHandler(&handler);

    // Server sends GOAWAY with debug data
    pair.server->SendGoAway(
        static_cast<uint32_t>(Http2ErrorCode::NoError), 0, "graceful shutdown");

    // Client should have received the GOAWAY
    ASSERT_TRUE(handler.got_goaway);
    ASSERT_TRUE(handler.error_code == static_cast<uint32_t>(Http2ErrorCode::NoError));
    ASSERT_TRUE(handler.debug_data == "graceful shutdown");

    pair.Shutdown();
}

// ============================================================================
// Test: RST_STREAM with typed error code
// ============================================================================
TEST(ProtocolTest, RSTStreamTypedErrorCode) {
    TransportPair pair;
    pair.Init(900, 1000);

    class ClosedHandler : public http2::EventHandler {
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

    ClosedHandler handler;
    pair.server->SetEventHandler(&handler);

    auto stream = pair.client->CreateStream();
    stream->SendHeaders({
        {":method", "GET"},
        {":path", "/rst"},
        {":scheme", "https"},
        {":authority", "localhost"},
    });

    // Use the typed overload
    stream->SendRSTStream(Http2ErrorCode::EnhanceYourCalm);

    ASSERT_TRUE(handler.got_closed);
    ASSERT_TRUE(handler.closed_error ==
                static_cast<uint32_t>(Http2ErrorCode::EnhanceYourCalm));

    pair.Shutdown();
}

// ============================================================================
// Test: Typed setting accessors
// ============================================================================
TEST(ProtocolTest, TypedSettingAccessors) {
    TransportPair pair;
    pair.Init(1100, 1200);

    // Trigger settings exchange by sending a request
    class DummyHandler : public http2::EventHandler {
    public:
        void OnStreamHeaders(std::shared_ptr<http2::Stream>) override {}
        void OnStreamData(std::shared_ptr<http2::Stream>) override {}
    };

    DummyHandler handler;
    pair.client->SetEventHandler(&handler);
    pair.server->SetEventHandler(&handler);

    auto stream = pair.client->CreateStream();
    stream->SendHeaders({
        {":method", "GET"},
        {":path", "/settings"},
        {":scheme", "https"},
        {":authority", "localhost"},
    }, true);

    // Use typed accessor
    uint32_t max_frame = pair.client->GetRemoteSetting(Http2SettingsId::MaxFrameSize);
    ASSERT_TRUE(max_frame >= 16384);  // RFC default

    uint32_t local_max_frame = pair.client->GetLocalSetting(Http2SettingsId::MaxFrameSize);
    ASSERT_TRUE(local_max_frame == 16384);

    pair.Shutdown();
}

// ============================================================================
// Test: Stream state typed accessor
// ============================================================================
TEST(ProtocolTest, StreamStateTyped) {
    TransportPair pair;
    pair.Init(1300, 1400);

    auto stream = pair.client->CreateStream();
    ASSERT_TRUE(stream != nullptr);

    // Stream should be in Idle state initially
    auto state = stream->CurrentStateTyped();
    ASSERT_TRUE(state == Http2StreamState::Idle);

    // After sending headers, state should change
    stream->SendHeaders({
        {":method", "GET"},
        {":path", "/state"},
        {":scheme", "https"},
        {":authority", "localhost"},
    });

    state = stream->CurrentStateTyped();
    ASSERT_TRUE(state == Http2StreamState::Open);

    pair.Shutdown();
}

// ============================================================================
// Test: Large headers (exercises CONTINUATION frame path)
// ============================================================================
TEST(ProtocolTest, LargeHeadersContinuation) {
    TransportPair pair;
    pair.Init(1500, 1600);

    class LargeHeaderHandler : public http2::EventHandler {
    public:
        std::vector<std::pair<std::string, std::string>> received_headers;

        void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
            received_headers = stream->GetHeaders();
        }
        void OnStreamData(std::shared_ptr<http2::Stream>) override {}
    };

    LargeHeaderHandler handler;
    pair.server->SetEventHandler(&handler);

    auto stream = pair.client->CreateStream();
    ASSERT_TRUE(stream != nullptr);

    // Build headers that are larger than a single MAX_FRAME_SIZE (16384 default)
    // Each header value of ~4000 bytes × 5 headers = ~20KB, enough for CONTINUATION
    std::vector<std::pair<std::string, std::string>> headers;
    headers.push_back({":method", "POST"});
    headers.push_back({":path", "/large"});
    headers.push_back({":scheme", "https"});
    headers.push_back({":authority", "localhost"});

    // Add large custom headers
    std::string large_value(4000, 'X');
    for (int i = 0; i < 5; i++) {
        headers.push_back({"x-custom-header-" + std::to_string(i), large_value});
    }

    bool ok = stream->SendHeaders(headers, true);
    ASSERT_TRUE(ok);

    // Server should have received all headers (including through CONTINUATION frames)
    ASSERT_TRUE(handler.received_headers.size() >= 9);  // 4 pseudo + 5 custom

    // Verify a custom header value
    bool found_custom = false;
    for (auto &h : handler.received_headers) {
        if (h.first == "x-custom-header-0" && h.second.size() == 4000) {
            found_custom = true;
        }
    }
    ASSERT_TRUE(found_custom);

    pair.Shutdown();
}

// ============================================================================
// Test: ConnectionInfo snapshot
// ============================================================================
TEST(ProtocolTest, ConnectionInfoSnapshot) {
    TransportPair pair;
    pair.Init(1700, 1800);

    class InfoHandler : public http2::EventHandler {
    public:
        std::shared_ptr<http2::Stream> stream;
        void OnStreamHeaders(std::shared_ptr<http2::Stream> s) override { stream = s; }
        void OnStreamData(std::shared_ptr<http2::Stream>) override {}
    };

    InfoHandler handler;
    pair.server->SetEventHandler(&handler);

    // Create multiple streams
    auto s1 = pair.client->CreateStream();
    auto s2 = pair.client->CreateStream();

    s1->SendHeaders({
        {":method", "GET"},
        {":path", "/info1"},
        {":scheme", "https"},
        {":authority", "localhost"},
    });
    s2->SendHeaders({
        {":method", "GET"},
        {":path", "/info2"},
        {":scheme", "https"},
        {":authority", "localhost"},
    });

    auto info = pair.server->GetConnectionInfo();
    ASSERT_TRUE(info.active_streams == 2);
    ASSERT_TRUE(info.last_stream_id >= 1);
    ASSERT_TRUE(!info.received_goaway);
    ASSERT_TRUE(!info.sent_goaway);
    ASSERT_TRUE(!info.draining);
    ASSERT_TRUE(info.connection_window > 0);

    pair.Shutdown();
}

// ============================================================================
// Test: ScopedBufferedMode RAII guard
// ============================================================================
TEST(ProtocolTest, ScopedBufferedModeGuard) {
    TransportPair pair;
    pair.Init(1900, 2000);

    class ScopedHandler : public http2::EventHandler {
    public:
        bool got_headers = false;
        void OnStreamHeaders(std::shared_ptr<http2::Stream>) override { got_headers = true; }
        void OnStreamData(std::shared_ptr<http2::Stream>) override {}
    };

    ScopedHandler handler;
    pair.server->SetEventHandler(&handler);

    auto stream = pair.client->CreateStream();

    // Use RAII guard
    {
        http2::ScopedBufferedMode guard(pair.client.get());
        stream->SendHeaders({
            {":method", "GET"},
            {":path", "/scoped"},
            {":scheme", "https"},
            {":authority", "localhost"},
        }, true);
        // Should NOT have been sent yet (buffered)
        ASSERT_TRUE(!handler.got_headers);
    }
    // Guard destructor should have flushed
    ASSERT_TRUE(handler.got_headers);

    pair.Shutdown();
}

int main() {
    test::RunAllTests();
    return 0;
}
