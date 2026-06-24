/**
 * @file integration_ext_test.cc
 * @brief Extended integration tests for http2::Transport API.
 *
 * Tests buffered mode, ping, settings exchange, and multiple streams
 * using the same LoopbackSendService pattern as integration_test.cc.
 */

#include "http2/transport.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "src/utils/testutil.h"

class ExtendedIntegrationTest {};

// ============================================================================
// LoopbackSendService -- wires two transports together in-memory
// ============================================================================

class LoopbackSendService : public http2::SendService {
public:
    http2::Transport *peer = nullptr;
    uint32_t total_bytes_sent = 0;
    bool was_called = false;

    int SendRawData(uint64_t /*cid*/, const uint8_t *buf, uint32_t size) override {
        was_called = true;
        total_bytes_sent += size;
        if (!peer) return -1;
        int consumed = peer->ReceivedData(buf, size);
        return consumed >= 0 ? static_cast<int>(size) : -1;
    }
};

// ============================================================================
// TestEventHandler -- records events for verification
// ============================================================================

class ExtendedEventHandler : public http2::EventHandler {
public:
    struct HeadersRecord {
        uint32_t stream_id;
        std::vector<std::pair<std::string, std::string>> headers;
    };

    struct DataRecord {
        uint32_t stream_id;
        std::string data;
    };

    std::vector<HeadersRecord> headers_events;
    std::vector<DataRecord> data_events;
    std::vector<std::pair<uint64_t, bool>> settings_events;
    std::vector<uint64_t> settings_ack_cids;
    std::vector<std::pair<uint64_t, uint64_t>> ping_events;

    void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
        HeadersRecord r;
        r.stream_id = stream->StreamId();
        r.headers = stream->GetHeaders();
        headers_events.push_back(std::move(r));
    }

    void OnStreamData(std::shared_ptr<http2::Stream> stream) override {
        DataRecord r;
        r.stream_id = stream->StreamId();
        uint32_t size = stream->DataSize();
        if (size > 0) {
            std::vector<uint8_t> buf(size);
            uint32_t read = stream->ReadData(buf.data(), size);
            r.data = std::string(reinterpret_cast<const char *>(buf.data()), read);
        }
        data_events.push_back(std::move(r));
    }

    void OnStreamClosed(std::shared_ptr<http2::Stream>, uint32_t) override {}

    void OnSettings(uint64_t cid,
                    const std::vector<std::pair<uint16_t, uint32_t>> &settings,
                    bool ack) override {
        settings_events.push_back({cid, ack});
        if (ack) {
            settings_ack_cids.push_back(cid);
        }
    }

    void OnPing(uint64_t cid, uint64_t data, bool /*ack*/) override {
        ping_events.push_back({cid, data});
    }

    void Reset() {
        headers_events.clear();
        data_events.clear();
        settings_events.clear();
        settings_ack_cids.clear();
        ping_events.clear();
    }
};

// ============================================================================
// Helper to set up a connected client+server pair
// ============================================================================

struct TransportPair {
    LoopbackSendService client_send;
    LoopbackSendService server_send;
    std::unique_ptr<http2::Transport> client;
    std::unique_ptr<http2::Transport> server;
    ExtendedEventHandler client_handler;
    ExtendedEventHandler server_handler;

    void Init(uint64_t client_cid = 1, uint64_t server_cid = 2) {
        client = http2::CreateTransport(client_cid, true, &client_send);
        server = http2::CreateTransport(server_cid, false, &server_send);
        client_send.peer = server.get();
        server_send.peer = client.get();
        client->SetEventHandler(&client_handler);
        server->SetEventHandler(&server_handler);
    }

    void Shutdown() {
        if (client) client->Shutdown();
        if (server) server->Shutdown();
    }
};

// ============================================================================
// TEST: BufferedMode -- enable buffered mode, send frames, flush
// ============================================================================
TEST(ExtendedIntegrationTest, BufferedMode) {
    TransportPair pair;
    pair.Init(100, 200);

    // Reset events from the automatic preface/settings exchange
    pair.client_handler.Reset();
    pair.server_handler.Reset();

    // Enable buffered mode on the client
    pair.client->SetBufferedMode(true);

    auto stream = pair.client->CreateStream();
    ASSERT_TRUE(stream != nullptr);

    stream->SendHeaders({
        {":method", "POST"},
        {":path", "/buffered"},
        {":scheme", "https"},
        {":authority", "localhost"},
    });

    const char *body = "buffered body data";
    stream->SendData(reinterpret_cast<const uint8_t *>(body),
                     static_cast<uint32_t>(strlen(body)), true);

    // Server should NOT have received the request yet (buffered)
    bool found_request = false;
    for (auto &h : pair.server_handler.headers_events) {
        for (auto &kv : h.headers) {
            if (kv.first == ":path" && kv.second == "/buffered") {
                found_request = true;
            }
        }
    }
    ASSERT_TRUE(!found_request);

    // Flush -- this should send all buffered data
    bool flushed = pair.client->Flush();
    ASSERT_TRUE(flushed);

    // Disable buffered mode
    pair.client->SetBufferedMode(false);

    // Now the server should have received the request
    found_request = false;
    for (auto &h : pair.server_handler.headers_events) {
        for (auto &kv : h.headers) {
            if (kv.first == ":path" && kv.second == "/buffered") {
                found_request = true;
            }
        }
    }
    ASSERT_TRUE(found_request);

    // Verify data was received
    bool found_data = false;
    for (auto &d : pair.server_handler.data_events) {
        if (d.data == "buffered body data") {
            found_data = true;
        }
    }
    ASSERT_TRUE(found_data);

    pair.Shutdown();
}

// ============================================================================
// TEST: PingPong -- client sends ping, server auto-acks
// ============================================================================
TEST(ExtendedIntegrationTest, PingPong) {
    TransportPair pair;
    pair.Init(300, 400);

    pair.client_handler.Reset();
    pair.server_handler.Reset();

    // Client sends a PING with data value 0xDEADBEEF
    uint64_t ping_data = 0xDEADBEEF;
    bool ok = pair.client->SendPing(ping_data);
    ASSERT_TRUE(ok);

    // Server should have received the ping (non-ack)
    ASSERT_TRUE(pair.server_handler.ping_events.size() >= 1);
    bool found_server_ping = false;
    for (auto &p : pair.server_handler.ping_events) {
        if (p.second == ping_data) {
            found_server_ping = true;
        }
    }
    ASSERT_TRUE(found_server_ping);

    // The library auto-sends a PING ACK back to the client.
    // The client should have received the ACK.
    ASSERT_TRUE(pair.client_handler.ping_events.size() >= 1);
    bool found_ack = false;
    for (auto &p : pair.client_handler.ping_events) {
        if (p.second == ping_data) {
            found_ack = true;
        }
    }
    ASSERT_TRUE(found_ack);

    pair.Shutdown();
}

// ============================================================================
// TEST: SettingsExchange -- both sides fire OnSettings after preface
// ============================================================================
TEST(ExtendedIntegrationTest, SettingsExchange) {
    TransportPair pair;
    pair.Init(500, 600);

    // After connection, both sides exchange SETTINGS.
    // The client sends its preface (SETTINGS), the server sends its SETTINGS
    // in response. Both sides should see OnSettings callbacks.

    // Client should have received the server's SETTINGS (non-ack)
    bool client_got_settings = false;
    for (auto &s : pair.client_handler.settings_events) {
        if (!s.second) {  // not an ack
            client_got_settings = true;
        }
    }
    ASSERT_TRUE(client_got_settings);

    // Server should have received the client's SETTINGS (non-ack)
    bool server_got_settings = false;
    for (auto &s : pair.server_handler.settings_events) {
        if (!s.second) {  // not an ack
            server_got_settings = true;
        }
    }
    ASSERT_TRUE(server_got_settings);

    // Both sides should also have received SETTINGS ACKs
    ASSERT_TRUE(pair.client_handler.settings_ack_cids.size() >= 1);
    ASSERT_TRUE(pair.server_handler.settings_ack_cids.size() >= 1);

    pair.Shutdown();
}

// ============================================================================
// TEST: MultipleStreams -- client creates 3 streams, server receives all
// ============================================================================
TEST(ExtendedIntegrationTest, MultipleStreams) {
    TransportPair pair;
    pair.Init(700, 800);

    pair.server_handler.Reset();

    // Create 3 streams and send headers on each
    auto s1 = pair.client->CreateStream();
    auto s2 = pair.client->CreateStream();
    auto s3 = pair.client->CreateStream();
    ASSERT_TRUE(s1 != nullptr);
    ASSERT_TRUE(s2 != nullptr);
    ASSERT_TRUE(s3 != nullptr);

    // Client stream IDs should be odd and sequential
    ASSERT_EQ(s1->StreamId(), 1);
    ASSERT_EQ(s2->StreamId(), 3);
    ASSERT_EQ(s3->StreamId(), 5);

    s1->SendHeaders({
        {":method", "GET"},
        {":path", "/stream1"},
        {":scheme", "https"},
        {":authority", "localhost"},
    }, true);

    s2->SendHeaders({
        {":method", "GET"},
        {":path", "/stream2"},
        {":scheme", "https"},
        {":authority", "localhost"},
    }, true);

    s3->SendHeaders({
        {":method", "GET"},
        {":path", "/stream3"},
        {":scheme", "https"},
        {":authority", "localhost"},
    }, true);

    // Server should have received headers for all 3 streams.
    // Count distinct stream IDs with actual request headers (not SETTINGS ACKs).
    std::vector<uint32_t> request_stream_ids;
    for (auto &h : pair.server_handler.headers_events) {
        for (auto &kv : h.headers) {
            if (kv.first == ":method") {
                request_stream_ids.push_back(h.stream_id);
                break;
            }
        }
    }

    ASSERT_EQ(request_stream_ids.size(), 3u);

    // Verify the stream IDs are correct (1, 3, 5)
    bool found_s1 = false, found_s2 = false, found_s3 = false;
    for (auto id : request_stream_ids) {
        if (id == 1) found_s1 = true;
        if (id == 3) found_s2 = true;
        if (id == 5) found_s3 = true;
    }
    ASSERT_TRUE(found_s1);
    ASSERT_TRUE(found_s2);
    ASSERT_TRUE(found_s3);

    // Verify the paths are correct
    bool found_path1 = false, found_path2 = false, found_path3 = false;
    for (auto &h : pair.server_handler.headers_events) {
        for (auto &kv : h.headers) {
            if (kv.first == ":path" && kv.second == "/stream1") found_path1 = true;
            if (kv.first == ":path" && kv.second == "/stream2") found_path2 = true;
            if (kv.first == ":path" && kv.second == "/stream3") found_path3 = true;
        }
    }
    ASSERT_TRUE(found_path1);
    ASSERT_TRUE(found_path2);
    ASSERT_TRUE(found_path3);

    pair.Shutdown();
}

int main() {
    test::RunAllTests();
    return 0;
}
