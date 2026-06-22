/**
 * @file stream.h
 * @brief HTTP/2 stream abstraction — manages per-stream state, headers,
 *        data buffering, priority information, and send operations.
 */

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "src/hpack/metadata.h"
#include "src/utils/slice_buffer.h"
#include "src/http2/frame.h"

#include "http2/transport.h"

class http2_connection;

/**
 * @brief HTTP/2 stream — tracks state machine, headers, data, and priority per stream.
 *
 * Inherits from http2::Stream (public interface) and enable_shared_from_this
 * so that shared_ptr<Stream> can be returned to users and passed to callbacks.
 *
 * The stream holds a non-owning back-pointer to its owning http2_connection
 * so that send operations (SendHeaders, SendData, etc.) can delegate to the
 * connection for HPACK encoding and wire serialization.
 */
class http2_stream : public http2::Stream, public std::enable_shared_from_this<http2_stream> {
public:
    /**
     * @brief Construct a stream with the given connection and stream IDs.
     * @param connection_id  Owning connection identifier.
     * @param stream_id      This stream's identifier.
     * @param conn           Non-owning pointer to the owning connection (for send operations).
     */
    http2_stream(uint64_t connection_id, uint32_t stream_id, http2_connection *conn);

    /** @brief Destructor. */
    ~http2_stream() {}

    // === State transitions (called by http2_connection on frame events) ===

    /** @brief Transition state on sending a PUSH_PROMISE frame. */
    void send_push_promise();

    /** @brief Transition state on receiving a PUSH_PROMISE frame. */
    void recv_push_promise();

    /** @brief Transition state on sending a HEADERS frame. */
    void send_headers();

    /** @brief Transition state on receiving a HEADERS frame and store decoded headers. */
    void recv_headers(std::vector<hpack::mdelem_data> &headers);

    /** @brief Transition state on sending a RST_STREAM frame. */
    void send_rst_stream();

    /**
     * @brief Transition state on receiving a RST_STREAM frame.
     * @param error_code  The error code from the RST_STREAM frame.
     */
    void recv_rst_stream(uint32_t error_code);

    /** @brief Transition state on sending END_STREAM and mark write-closed. */
    void send_end_stream();

    /** @brief Transition state on receiving END_STREAM and mark read-closed. */
    void recv_end_stream();

    // === Frame info ===

    /** @brief Return the frame type of the last processed frame. */
    uint8_t frame_type();

    /** @brief Return the frame flags of the last processed frame. */
    uint8_t frame_flags();

    /** @brief Save frame header info and transition state if END_STREAM is set. */
    void save_frame_info(http2_frame_hdr *hdr);

    // === Stream info ===

    /** @brief Return the current HTTP/2 stream state. */
    int get_state() const;

    /** @brief Return true if the stream is in the Closed state. */
    bool is_closed() const;

    /** @brief Return the stream identifier. */
    uint32_t stream_id() const;

    // === Data management ===

    /** @brief Append decoded headers to the stream's header list. */
    void append_headers(const std::vector<hpack::mdelem_data> &headers);

    /** @brief Append a data slice to the stream's data buffer. */
    void append_data(slice s);

    // === Priority ===

    /** @brief Set the priority specification for this stream. */
    void set_priority_info(http2_priority_spec *info);

    // === Read/write control ===

    /** @brief Mark the stream as write-closed (no more data can be sent). */
    void mark_unwritable();

    /** @brief Mark the stream as read-closed (no more data expected). */
    void mark_unreadable();

    /** @brief Return a shared_ptr to this stream via enable_shared_from_this. */
    std::shared_ptr<http2::Stream> get_shared_stream();

    // === http2::Stream interface implementation ===

    uint64_t ConnectionId() const override;
    uint32_t StreamId() const override;
    int Flags() const override;
    uint32_t ErrorCode() const override;
    int CurrentState() const override;

    // Send operations — delegate to http2_connection
    bool SendHeaders(const std::vector<std::pair<std::string, std::string>> &headers,
                     bool end_stream) override;
    bool SendData(const uint8_t *data, uint32_t size, bool end_stream) override;
    bool SendTrailingHeaders(
        const std::vector<std::pair<std::string, std::string>> &headers) override;
    bool SendRSTStream(uint32_t error_code) override;

    // Data reading
    uint32_t DataSize() const override;
    uint32_t ReadData(uint8_t *buffer, uint32_t size) override;
    const uint8_t *PeekData(uint32_t *out_size) const override;

    // Header reading
    const std::vector<std::pair<std::string, std::string>> &GetHeaders() const override;

private:
    /** @brief Convert internal mdelem_data headers to public pair format. */
    void ensure_headers_decoded() const;

    uint64_t _connection_id;
    uint32_t _stream_id;

    int _state;

    uint8_t _frame_flags;
    uint8_t _frame_type;

    bool _read_closed;
    bool _write_closed;

    http2_priority_spec _spec;

    uint32_t _last_error;
    slice_buffer _data_cache;

    // Internal HPACK-decoded headers (efficient, zero-copy slices)
    std::vector<hpack::mdelem_data> _headers;

    // Public header pairs (lazily converted from _headers on first access)
    mutable std::vector<std::pair<std::string, std::string>> _public_headers;
    mutable bool _headers_dirty;

    // Non-owning back-pointer to the owning connection (for send delegation)
    http2_connection *_conn;
};
