/**
 * @file test_multi_stream_crash.cc
 * @brief Reproducer for multi-stream crash: server responds then second request.
 */

#include "http2/transport.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "src/utils/testutil.h"

class MultiStreamCrashTest {};

class LoopbackSendService : public http2::SendService {
public:
    http2::Transport *peer = nullptr;
    const char *name = "?";
    int SendRawData(uint64_t, const uint8_t *buf, uint32_t size) override {
        if (!peer) return -1;
        int consumed = peer->ReceivedData(buf, size);
        if (consumed < 0) {
            fprintf(stderr, "  [%s] SendRawData: peer->ReceivedData returned %d (ERROR)\n", name, consumed);
        }
        return consumed >= 0 ? static_cast<int>(size) : -1;
    }
};

class DebugServer : public http2::EventHandler {
public:
    int response_count = 0;
    std::vector<uint32_t> received_stream_ids;

    void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
        fprintf(stderr, "  [server] OnStreamHeaders: stream_id=%d state=%d\n",
                stream->StreamId(), stream->CurrentState());
        received_stream_ids.push_back(stream->StreamId());
        bool ok = stream->SendHeaders({{":status", "200"}}, true);
        fprintf(stderr, "  [server] SendHeaders response: %s, state=%d\n",
                ok ? "ok" : "FAIL", stream->CurrentState());
        response_count++;
    }
    void OnStreamData(std::shared_ptr<http2::Stream>) override {}
    void OnGoAway(uint64_t cid, uint32_t last_stream_id, uint32_t error_code,
                  const std::string &debug) override {
        fprintf(stderr, "  [server] OnGoAway: cid=%llu last_stream=%u error=%u debug='%s'\n",
                (unsigned long long)cid, last_stream_id, error_code, debug.c_str());
    }
};

class DebugClient : public http2::EventHandler {
public:
    int headers_count = 0;

    void OnStreamHeaders(std::shared_ptr<http2::Stream> stream) override {
        fprintf(stderr, "  [client] OnStreamHeaders: stream_id=%d state=%d\n",
                stream->StreamId(), stream->CurrentState());
        headers_count++;
    }
    void OnStreamData(std::shared_ptr<http2::Stream>) override {}
    void OnGoAway(uint64_t cid, uint32_t last_stream_id, uint32_t error_code,
                  const std::string &debug) override {
        fprintf(stderr, "  [client] OnGoAway: cid=%llu last_stream=%u error=%u debug='%s'\n",
                (unsigned long long)cid, last_stream_id, error_code, debug.c_str());
    }
};

TEST(MultiStreamCrashTest, TwoSequentialRequests) {
    LoopbackSendService client_send;
    LoopbackSendService server_send;
    client_send.name = "client_send";
    server_send.name = "server_send";

    auto client = http2::CreateTransport(1, true, &client_send);
    auto server = http2::CreateTransport(2, false, &server_send);

    client_send.peer = server.get();
    server_send.peer = client.get();

    DebugServer server_handler;
    DebugClient client_handler;
    client->SetEventHandler(&client_handler);
    server->SetEventHandler(&server_handler);

    // --- Request 1 ---
    fprintf(stderr, "--- Creating stream 1 ---\n");
    auto stream1 = client->CreateStream();
    fprintf(stderr, "stream1=%p id=%d\n", (void*)stream1.get(), stream1 ? stream1->StreamId() : -1);

    fprintf(stderr, "--- Sending request 1 ---\n");
    stream1->SendHeaders({
        {":method", "GET"},
        {":path", "/first"},
        {":scheme", "https"},
        {":authority", "localhost"},
    }, true);

    fprintf(stderr, "--- After request 1: server received %zu, client got %d responses ---\n",
            server_handler.received_stream_ids.size(), client_handler.headers_count);

    // --- Request 2 ---
    fprintf(stderr, "--- Creating stream 3 ---\n");
    auto stream3 = client->CreateStream();
    fprintf(stderr, "stream3=%p\n", (void*)stream3.get());
    if (!stream3) {
        fprintf(stderr, "FATAL: CreateStream returned nullptr! _received_goaway=true\n");
        auto info = client->GetConnectionInfo();
        fprintf(stderr, "  client: received_goaway=%d sent_goaway=%d active_streams=%u\n",
                info.received_goaway, info.sent_goaway, info.active_streams);
        auto sinfo = server->GetConnectionInfo();
        fprintf(stderr, "  server: received_goaway=%d sent_goaway=%d active_streams=%u\n",
                sinfo.received_goaway, sinfo.sent_goaway, sinfo.active_streams);
    }

    client->Shutdown();
    server->Shutdown();
}

int main() {
    test::RunAllTests();
    return 0;
}
