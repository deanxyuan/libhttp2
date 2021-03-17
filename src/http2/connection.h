#pragma once
#include <stdint.h>
#include <memory>
#include <map>
#include <mutex>

#include "src/hpack/send_record.h"
#include "src/hpack/dynamic_metadata.h"
#include "src/http2/frame.h"
#include "src/utils/slice_buffer.h"

#include "http2/transport.h"

class http2_stream;
class http2_connection {
public:
    // "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
    static const uint8_t PREFACE[24];
    static constexpr int PREFACE_SIZE = 24;
    static constexpr int INITIAL_WINDOW_SIZE = 4 * 1024 * 1024;
    static constexpr int MAX_FRAME_SIZE = 4 * 1024 * 1024;
    static constexpr int MAX_HEADER_LIST_SIZE = 8192;

    http2_connection(http2::SendService *sender, uint64_t cid, bool client_side);
    ~http2_connection();

    uint64_t connection_id() const;
    bool is_client_side() const;

    void set_frame_event_handler(http2::FrameEventHandler *h);
    void set_flow_control_handler(http2::FlowControlHandler *h);

    // for server side use
    bool need_verify_preface() const;
    void verify_preface_done();

    bool send_ping(uint64_t info);
    bool send_settings(const std::vector<std::pair<uint16_t, uint32_t>> &settings);
    bool send_priority(uint32_t stream_id, uint8_t weight, uint32_t depend_stream_id = 0);
    bool send_rst_stream(uint32_t stream_id, uint32_t error_code);

    void send_goaway(uint32_t error_code, uint32_t last_stream_id = 0, const std::string &debug = std::string());

    bool send_window_update(uint32_t stream_id, http2::WindowUpdate *wu);

    bool send_request(http2::Request *request);
    bool send_response(http2::Response *response);

    uint32_t send_push_promise(std::vector<std::pair<std::string, std::string>> *initlize_headers);

    int package_process(const uint8_t *data, uint32_t len);
    // void async_send_response(std::shared_ptr<http2_response> rsp);

    inline uint32_t local_max_frame_size() const {
        return _local_settings[HTTP2_SETTINGS_MAX_FRAME_SIZE];
    }

    // if fail return 0
    uint32_t create_stream();

    std::shared_ptr<http2_stream> find_stream(uint32_t stream_id);

    void received_data(std::shared_ptr<http2_stream> &stream, http2_frame_data *frame);
    void received_headers(std::shared_ptr<http2_stream> &stream, http2_frame_headers *frame);
    void received_priority(std::shared_ptr<http2_stream> &stream, http2_frame_priority *frame);
    void received_rst_stream(std::shared_ptr<http2_stream> &stream, http2_frame_rst_stream *frame);
    void received_settings(http2_frame_settings *frame);
    void received_push_promise(std::shared_ptr<http2_stream> &stream, http2_frame_push_promise *frame);
    void received_ping(http2_frame_ping *frame);
    void received_goaway(http2_frame_goaway *frame);
    void received_window_update(std::shared_ptr<http2_stream> &stream, http2_frame_window_update *frame);
    void received_continuation(std::shared_ptr<http2_stream> &stream, http2_frame_continuation *frame);

private:
    void send_raw_data(const slice_buffer &sb);
    void send_raw_data(slice s);

    inline uint32_t local_settings(int setting_id) const {
        return _local_settings[setting_id];
    }

    void send_http2_frame(http2_frame_data *);
    void send_http2_frame(http2_frame_headers *);
    void send_http2_frame(http2_frame_priority *);
    void send_http2_frame(http2_frame_rst_stream *);
    void send_http2_frame(http2_frame_settings *);
    void send_http2_frame(http2_frame_push_promise *);
    void send_http2_frame(http2_frame_ping *);
    void send_http2_frame(http2_frame_goaway *);
    void send_http2_frame(http2_frame_window_update *);

    void destroy_stream(uint32_t stream_id);
    void send_binary_in_headers_frame(std::shared_ptr<http2_stream> &stream,
                                      const std::vector<std::pair<std::string, std::string>> &headers, int flags);

    void send_binary_in_data_frame(std::shared_ptr<http2_stream> &stream, const std::vector<std::string> &binary,
                                   bool end_of_stream);

private:
    hpack::dynamic_metadata_table _dynamic_table;
    http2::SendService *_sender_service;
    http2::FrameEventHandler *_event_handler;
    http2::FlowControlHandler *_flow_control;

    uint64_t _connection_id;
    bool _client_side;
    bool _ping_pending;
    bool _settings_pending;

    uint32_t _local_settings[HTTP2_NUMBER_OF_SETTINGS];
    uint32_t _remote_settings[HTTP2_NUMBER_OF_SETTINGS];

    bool _finish_handshake;
    uint32_t _last_stream_id;

    // local stream id
    uint32_t _next_local_stream_id;

    std::map<uint32_t, std::shared_ptr<http2_stream>> _streams;

    hpack::compressor _send_record;

    uint32_t _received_goaway_stream_id;
    bool _received_goaway;

    uint32_t _sent_goaway_stream_id;
    bool _sent_goaway;

    // Because the END_HEADERS flag is missing
    bool _next_frame_limit;
    uint32_t _next_stream_id_limit;

    std::vector<std::pair<uint16_t, uint32_t>> _request_settings;

    std::mutex _mutex;
};
