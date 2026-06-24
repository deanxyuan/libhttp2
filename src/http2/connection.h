/**
 * @file connection.h
 * @brief HTTP/2 connection management -- the http2_connection class that owns
 *        streams, settings, HPACK state, and frame dispatch.
 */

#pragma once
#include <stdint.h>
#include <memory>
#include <map>

#include "src/hpack/send_record.h"
#include "src/hpack/dynamic_metadata.h"
#include "src/http2/frame.h"
#include "src/utils/slice_buffer.h"

#include "http2/transport.h"

class http2_stream;

/**
 * @brief Core HTTP/2 connection state machine.
 *
 * Manages stream lifecycle, settings negotiation, HPACK compression/decompression,
 * flow control, and frame dispatch for a single HTTP/2 connection.
 */
class http2_connection {
public:
    /** @brief HTTP/2 client connection preface bytes: "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n" */
    static const uint8_t PREFACE[24];
    /** @brief Size of the connection preface in bytes. */
    static constexpr int PREFACE_SIZE = 24;
    /** @brief RFC 7540 default initial window size. */
    static constexpr int INITIAL_WINDOW_SIZE = 65535;
    /** @brief RFC 7540 default maximum frame size. */
    static constexpr int MAX_FRAME_SIZE = 16384;
    /** @brief Default maximum header list size. */
    static constexpr int MAX_HEADER_LIST_SIZE = 8192;

    /**
     * @brief Construct an HTTP/2 connection.
     * @param sender     SendService used for raw data output.
     * @param cid        Unique connection identifier.
     * @param client_side  True if this endpoint is the HTTP/2 client.
     */
    http2_connection(http2::SendService *sender, uint64_t cid, bool client_side);

    /** @brief Destructor -- releases HPACK compressor resources. */
    ~http2_connection();

    /** @brief Return the connection identifier. */
    uint64_t connection_id() const;

    /** @brief Return whether this endpoint is the client side. */
    bool is_client_side() const;

    /** @brief Set the event handler callback. */
    void set_event_handler(http2::EventHandler *h);

    /** @brief Set the flow-control event handler callback (nullptr = automatic). */
    void set_flow_control_handler(http2::FlowControlHandler *h);

    /** @brief Check whether the client preface still needs to be verified (server side). */
    bool need_verify_preface() const;

    /** @brief Mark the client preface as verified. */
    void verify_preface_done();

    /**
     * @brief Send a PING frame with the given opaque data.
     * @return True if the ping was sent; false if a ping is already pending.
     */
    bool send_ping(uint64_t info);

    /**
     * @brief Send a SETTINGS frame with the given key-value pairs.
     * @return True if the settings were sent; false if settings are already pending or input is empty.
     */
    bool send_settings(const std::vector<std::pair<uint16_t, uint32_t>> &settings);

    /**
     * @brief Send a RST_STREAM frame to terminate a stream.
     * @return True on success; false if the stream does not exist.
     */
    bool send_rst_stream(uint32_t stream_id, uint32_t error_code);

    /** @brief Send a GOAWAY frame to initiate connection shutdown. */
    bool send_goaway(uint32_t error_code, uint32_t last_stream_id = 0, const std::string &debug = std::string());

    /** @brief Begin a graceful shutdown (drain): send GOAWAY, reject new streams, wait for completion. */
    void drain();

    /** @brief Send a PUSH_PROMISE frame. Returns the promised stream. */
    std::shared_ptr<http2_stream> send_push_promise(
        http2_stream *request_stream,
        const std::vector<std::pair<std::string, std::string>> &headers);

    /** @brief Send the initial default SETTINGS frame (auto-called on server after preface verification). */
    void send_initial_settings();

    /**
     * @brief Send a WINDOW_UPDATE frame for connection or stream flow control.
     * @return True if at least one WINDOW_UPDATE was sent.
     */
    bool send_window_update(uint32_t stream_id, const http2::WindowUpdate &wu);

    // === Stream-level send operations (called by http2_stream) ===

    /** @brief Send HEADERS frame on behalf of a stream. */
    bool stream_send_headers(http2_stream *stream,
                             const std::vector<std::pair<std::string, std::string>> &headers,
                             bool end_stream);

    /** @brief Send DATA frames on behalf of a stream. */
    bool stream_send_data(http2_stream *stream, const uint8_t *data, uint32_t size,
                          bool end_stream);

    /** @brief Send trailing HEADERS frame (with END_STREAM) on behalf of a stream. */
    bool stream_send_trailing_headers(http2_stream *stream,
                                      const std::vector<std::pair<std::string, std::string>> &headers);

    /** @brief Send RST_STREAM on behalf of a stream. */
    bool stream_send_rst_stream(http2_stream *stream, uint32_t error_code);

    /**
     * @brief Process a single complete HTTP/2 frame from the raw data buffer.
     * @param data  Pointer to the start of the frame (including 9-byte header).
     * @param len   Total bytes available (must be >= frame length + 9).
     * @return The number of bytes consumed, or -1 on protocol error.
     */
    int package_process(const uint8_t *data, uint32_t len);

    /** @brief Return the local MAX_FRAME_SIZE setting value. */
    inline uint32_t local_max_frame_size() const {
        return _local_settings[static_cast<size_t>(Http2SettingsId::MaxFrameSize)];
    }

    /**
     * @brief Allocate a new local stream ID and register the stream.
     * @return Shared pointer to the new stream, or nullptr on failure.
     */
    std::shared_ptr<http2_stream> create_stream();

    /**
     * @brief Look up a stream by its ID.
     * @return Shared pointer to the stream, or nullptr if not found.
     */
    std::shared_ptr<http2_stream> find_stream(uint32_t stream_id);

    /** @brief Get a remote SETTINGS value by parameter ID. */
    uint32_t remote_setting(int id) const { return _remote_settings[id]; }

    /** @brief Get a local SETTINGS value by parameter ID. */
    uint32_t local_setting(int id) const { return _local_settings[id]; }

    /** @brief Get a snapshot of connection-level state. */
    http2::ConnectionInfo get_connection_info() const;

    /** @brief Enable or disable send buffering. */
    void set_buffered_mode(bool enable);

    /** @brief Flush accumulated buffered data via SendRawData.
     *  @return true if all data was sent successfully, false on error.
     */
    bool flush_buffer();

    /** @brief Append data to the send buffer instead of sending immediately. */
    void buffer_raw_data(slice s);
    void buffer_raw_data(const slice_buffer &sb);

    /** @brief Handle a received DATA frame. */
    void received_data(std::shared_ptr<http2_stream> &stream, http2_frame_data *frame);

    /** @brief Handle a received HEADERS frame (may create a new stream). */
    void received_headers(std::shared_ptr<http2_stream> &stream, http2_frame_headers *frame);

    /** @brief Handle a received PRIORITY frame. */
    void received_priority(std::shared_ptr<http2_stream> &stream, http2_frame_priority *frame);

    /** @brief Handle a received RST_STREAM frame. */
    void received_rst_stream(std::shared_ptr<http2_stream> &stream, http2_frame_rst_stream *frame);

    /** @brief Handle a received SETTINGS frame (or SETTINGS ACK). */
    void received_settings(http2_frame_settings *frame);

    /** @brief Handle a received PUSH_PROMISE frame. */
    void received_push_promise(std::shared_ptr<http2_stream> &stream, http2_frame_push_promise *frame);

    /** @brief Handle a received PING frame (send ACK if not already an ACK). */
    void received_ping(http2_frame_ping *frame);

    /** @brief Handle a received GOAWAY frame. */
    void received_goaway(http2_frame_goaway *frame);

    /** @brief Handle a received WINDOW_UPDATE frame. */
    void received_window_update(std::shared_ptr<http2_stream> &stream, http2_frame_window_update *frame);

    /** @brief Handle a received CONTINUATION frame (appends to incomplete HEADERS). */
    void received_continuation(std::shared_ptr<http2_stream> &stream, http2_frame_continuation *frame);

private:
    /** @brief Send raw binary data from a slice_buffer via the SendService.
     *  @return Total bytes sent/queued, or -1 on error.
     */
    int send_raw_data(const slice_buffer &sb);

    /** @brief Send raw binary data from a single slice via the SendService.
     *  @return Bytes sent/queued, or -1 on error.
     */
    int send_raw_data(slice s);

    /** @brief Return a local setting value by index. */
    inline uint32_t local_settings(int setting_id) const {
        return _local_settings[setting_id];
    }

    /** @brief Serialize and send a DATA frame (may split into multiple frames). */
    void send_http2_frame(http2_frame_data *);

    /** @brief Serialize and send a HEADERS frame. */
    void send_http2_frame(http2_frame_headers *);

    /** @brief Serialize and send a PRIORITY frame. */
    void send_http2_frame(http2_frame_priority *);

    /** @brief Serialize and send a RST_STREAM frame. */
    void send_http2_frame(http2_frame_rst_stream *);

    /** @brief Serialize and send a SETTINGS frame. */
    void send_http2_frame(http2_frame_settings *);

    /** @brief Serialize and send a PUSH_PROMISE frame. */
    void send_http2_frame(http2_frame_push_promise *);

    /** @brief Serialize and send a PING frame. */
    void send_http2_frame(http2_frame_ping *);

    /** @brief Serialize and send a GOAWAY frame. */
    void send_http2_frame(http2_frame_goaway *);

    /** @brief Serialize and send a WINDOW_UPDATE frame. */
    void send_http2_frame(http2_frame_window_update *);

    /** @brief Remove a stream from the connection's stream map. */
    void destroy_stream(uint32_t stream_id);

    /** @brief HPACK-encode headers and send them in a HEADERS frame. */
    void send_binary_in_headers_frame(std::shared_ptr<http2_stream> &stream,
                                      const std::vector<std::pair<std::string, std::string>> &headers, int flags);

    /** @brief Send binary data blocks in one or more DATA frames with flow control. */
    void send_binary_in_data_frame(std::shared_ptr<http2_stream> &stream, const uint8_t *data,
                                   uint32_t size, bool end_of_stream);

    /** @brief Notify the event handler that a stream has been closed. */
    void notify_stream_closed(std::shared_ptr<http2_stream> &stream, uint32_t error_code);

    /** @brief Check if drain is complete (all streams closed) and fire OnShutdownComplete. */
    void check_drain_complete();

    /** @brief Lazily send the HTTP/2 client connection preface + SETTINGS on first send (client side only). */
    void ensure_preface_sent();

private:
    hpack::dynamic_metadata_table _dynamic_table;
    http2::SendService *_sender_service;
    http2::EventHandler *_event_handler;
    http2::FlowControlHandler *_flow_control;

    uint64_t _connection_id;
    bool _client_side;
    bool _ping_pending;
    bool _settings_pending;
    bool _preface_sent;
    bool _initial_settings_sent;

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
    bool _draining;

    // Because the END_HEADERS flag is missing
    bool _next_frame_limit;
    uint32_t _next_stream_id_limit;

    std::vector<std::pair<uint16_t, uint32_t>> _request_settings;

    bool _buffered_mode;
    slice_buffer _send_buffer;
};
