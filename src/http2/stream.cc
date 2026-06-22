/**
 * @file stream.cc
 * @brief HTTP/2 stream state machine implementation — transitions, data buffering,
 *        and the public Stream interface methods.
 */

/* http2 status migrational table
                                +--------+
                        send PP |        | recv PP
                       ,--------|  idle  |--------.
                      /         |        |         \
                     v          +--------+          v
              +----------+          |           +----------+
              |          |          | send H /  |          |
       ,------| reserved |          | recv H    | reserved |------.
       |      | (local)  |          |           | (remote) |      |
       |      +----------+          v           +----------+      |
       |          |             +--------+             |          |
       |          |     recv ES |        | send ES     |          |
       |   send H |     ,-------|  open  |-------.     | recv H   |
       |          |    /        |        |        \    |          |
       |          v   v         +--------+         v   v          |
       |      +----------+          |           +----------+      |
       |      |   half   |          |           |   half   |      |
       |      |  closed  |          | send R /  |  closed  |      |
       |      | (remote) |          | recv R    | (local)  |      |
       |      +----------+          |           +----------+      |
       |           |                |                 |           |
       |           | send ES /      |       recv ES / |           |
       |           | send R /       v        send R / |           |
       |           | recv R     +--------+   recv R   |           |
       | send R /  '----------->|        |<-----------'  send R / |
       | recv R                 | closed |               recv R   |
       '----------------------->|        |<----------------------'
                                +--------+
          send:   endpoint sends this frame
          recv:   endpoint receives this frame
          H:  HEADERS frame (with implied CONTINUATIONs)
          PP: PUSH_PROMISE frame (with implied CONTINUATIONs)
          ES: END_STREAM flag
          R:  RST_STREAM frame
*/

#include "src/http2/stream.h"
#include "src/http2/connection.h"
#include <assert.h>
#include "src/http2/frame.h"

/** @brief Stream events used for state machine transitions. */
enum http2_stream_event {
    STREAM_EVENT_H_R = 0,  // HEADERS Frame recv
    STREAM_EVENT_H_S,      // HEADERS Frame send
    STREAM_EVENT_PP_R,     // PUSH_PROMISE
    STREAM_EVENT_PP_S,     // PUSH_PROMISE
    STREAM_EVENT_ES_R,     // END_STREAM Recv
    STREAM_EVENT_ES_S,     // END_STREAM Send
    STREAM_EVENT_RST_R,    // RST_STREAM
    STREAM_EVENT_RST_S,    // RST_STREAM

    _STREAM_EVENT_COUNTER
};

namespace internal {
constexpr int kIdle = static_cast<int>(Http2StreamState::Idle);
constexpr int kReservedLocal = static_cast<int>(Http2StreamState::ReservedLocal);
constexpr int kReservedRemote = static_cast<int>(Http2StreamState::ReservedRemote);
constexpr int kOpen = static_cast<int>(Http2StreamState::Open);
constexpr int kHalfClosedLocal = static_cast<int>(Http2StreamState::HalfClosedLocal);
constexpr int kHalfClosedRemote = static_cast<int>(Http2StreamState::HalfClosedRemote);
constexpr int kClosed = static_cast<int>(Http2StreamState::Closed);
constexpr int kDoNotUse = kClosed + 1;
}  // namespace internal

/** @brief State transition table: event_status_table[event][current_state] = next_state. */
const int event_status_table[8][internal::kDoNotUse] = {
    {
        // Recv HEADERS Frame
        internal::kOpen,             // when kIdle
        internal::kDoNotUse,         // when kReservedLocal
        internal::kHalfClosedLocal,  // when kReservedRemote
        internal::kDoNotUse,         // when kOpen
        internal::kDoNotUse,         // when kHalfClosedLocal
        internal::kDoNotUse,         // when kHalfClosedRemote
        internal::kDoNotUse          // when kClosed
    },
    {
        // Send HEADERS Frame
        internal::kOpen,              // when kIdle
        internal::kHalfClosedRemote,  // when kReservedLocal
        internal::kDoNotUse,          // when kReservedRemote
        internal::kDoNotUse,          // when kOpen
        internal::kDoNotUse,          // when kHalfClosedLocal
        internal::kDoNotUse,          // when kHalfClosedRemote
        internal::kDoNotUse           // when kClosed
    },
    {
        // Recv PUSH_PROMISE
        internal::kHalfClosedRemote,  // when kIdle
        internal::kDoNotUse,          // when kReservedLocal
        internal::kDoNotUse,          // when kReservedRemote
        internal::kDoNotUse,          // when kOpen
        internal::kDoNotUse,          // when kHalfClosedLocal
        internal::kDoNotUse,          // when kHalfClosedRemote
        internal::kDoNotUse           // when kClosed
    },
    {
        // Send PUSH_PROMISE
        internal::kReservedLocal,  // when kIdle
        internal::kDoNotUse,       // when kReservedLocal
        internal::kDoNotUse,       // when kReservedRemote
        internal::kDoNotUse,       // when kOpen
        internal::kDoNotUse,       // when kHalfClosedLocal
        internal::kDoNotUse,       // when kHalfClosedRemote
        internal::kDoNotUse        // when kClosed
    },
    {
        // Recv END_STREAM
        internal::kDoNotUse,          // when kIdle
        internal::kDoNotUse,          // when kReservedLocal
        internal::kDoNotUse,          // when kReservedRemote
        internal::kHalfClosedRemote,  // when kOpen
        internal::kClosed,            // when kHalfClosedLocal
        internal::kDoNotUse,          // when kHalfClosedRemote
        internal::kDoNotUse           // when kClosed
    },
    {
        // Send END_STREAM
        internal::kDoNotUse,         // when kIdle
        internal::kDoNotUse,         // when kReservedLocal
        internal::kDoNotUse,         // when kReservedRemote
        internal::kHalfClosedLocal,  // when kOpen
        internal::kDoNotUse,         // when kHalfClosedLocal
        internal::kClosed,           // when kHalfClosedRemote
        internal::kDoNotUse          // when kClosed
    },
    {
        // Recv RST_STREAM
        internal::kDoNotUse,  // when kIdle
        internal::kClosed,    // when kReservedLocal
        internal::kClosed,    // when kReservedRemote
        internal::kClosed,    // when kOpen
        internal::kClosed,    // when kHalfClosedLocal
        internal::kClosed,    // when kHalfClosedRemote
        internal::kDoNotUse   // when kClosed
    },
    {
        // Send RST_STREAM
        internal::kDoNotUse,  // when kIdle
        internal::kClosed,    // when kReservedLocal
        internal::kClosed,    // when kReservedRemote
        internal::kClosed,    // when kOpen
        internal::kClosed,    // when kHalfClosedLocal
        internal::kClosed,    // when kHalfClosedRemote
        internal::kDoNotUse   // when kClosed
    },
};

/** @brief Look up the next state for a given event and current state. */
static inline int get_next_status(http2_stream_event event, int current) {
    return event_status_table[static_cast<int>(event)][current];
}

// ============================================================================
// State machine transitions (unchanged)
// ============================================================================

/** @brief Transition state on sending a PUSH_PROMISE frame. */
void http2_stream::send_push_promise() {
    auto next_state = get_next_status(STREAM_EVENT_PP_S, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
}

/** @brief Transition state on receiving a PUSH_PROMISE frame. */
void http2_stream::recv_push_promise() {
    auto next_state = get_next_status(STREAM_EVENT_PP_R, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
}

/** @brief Transition state on sending a HEADERS frame. */
void http2_stream::send_headers() {
    auto next_state = get_next_status(STREAM_EVENT_H_S, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
}

/** @brief Transition state on receiving a HEADERS frame and store decoded headers. */
void http2_stream::recv_headers(std::vector<hpack::mdelem_data> &headers) {
    auto next_state = get_next_status(STREAM_EVENT_H_R, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
    _headers = std::move(headers);
    _headers_dirty = true;
}

void http2_stream::send_rst_stream() {
    auto next_state = get_next_status(STREAM_EVENT_RST_S, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
}

void http2_stream::recv_rst_stream(uint32_t error_code) {
    auto next_state = get_next_status(STREAM_EVENT_RST_R, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
    _last_error = error_code;
}

void http2_stream::send_end_stream() {
    auto next_state = get_next_status(STREAM_EVENT_ES_S, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
    mark_unwritable();
}

void http2_stream::recv_end_stream() {
    auto next_state = get_next_status(STREAM_EVENT_ES_R, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
    mark_unreadable();
}

// ============================================================================
// Constructor
// ============================================================================

http2_stream::http2_stream(uint64_t connection_id, uint32_t stream_id, http2_connection *conn)
    : _connection_id(connection_id)
    , _stream_id(stream_id)
    , _state(internal::kIdle)
    , _frame_flags(0)
    , _frame_type(0)
    , _read_closed(false)
    , _write_closed(false)
    , _last_error(0)
    , _headers_dirty(true)
    , _conn(conn) {
    _spec.depend_stream_id = 0;
    _spec.exclusive = false;
    _spec.weight = 16;
}

// ============================================================================
// Frame info
// ============================================================================

uint8_t http2_stream::frame_type() {
    return _frame_type;
}

uint8_t http2_stream::frame_flags() {
    return _frame_flags;
}

void http2_stream::save_frame_info(http2_frame_hdr *hdr) {
    _frame_type = hdr->type;
    _frame_flags = hdr->flags;
    if (_frame_flags & static_cast<uint8_t>(Http2FrameFlag::EndStream)) {
        recv_end_stream();
    }
}

// ============================================================================
// Data management
// ============================================================================

void http2_stream::append_headers(const std::vector<hpack::mdelem_data> &headers) {
    for (size_t i = 0; i < headers.size(); i++) {
        _headers.emplace_back(headers[i]);
    }
    _headers_dirty = true;
}

void http2_stream::append_data(slice s) {
    slice obj(s.data(), s.size());
    _data_cache.add_slice(obj);
}

// ============================================================================
// Stream info
// ============================================================================

bool http2_stream::is_closed() const {
    return _state == internal::kClosed;
}

uint32_t http2_stream::stream_id() const {
    return _stream_id;
}

int http2_stream::get_state() const {
    return _state;
}

// ============================================================================
// Priority
// ============================================================================

void http2_stream::set_priority_info(http2_priority_spec *info) {
    _spec = *info;
}

// ============================================================================
// Read/write control
// ============================================================================

void http2_stream::mark_unwritable() {
    _write_closed = true;
}

void http2_stream::mark_unreadable() {
    _read_closed = true;
}

std::shared_ptr<http2::Stream> http2_stream::get_shared_stream() {
    return shared_from_this();
}

// ============================================================================
// http2::Stream interface — read-only state
// ============================================================================

uint64_t http2_stream::ConnectionId() const {
    return _connection_id;
}

uint32_t http2_stream::StreamId() const {
    return _stream_id;
}

int http2_stream::Flags() const {
    return _frame_flags;
}

uint32_t http2_stream::ErrorCode() const {
    return _last_error;
}

int http2_stream::CurrentState() const {
    return _state;
}

// ============================================================================
// http2::Stream interface — send operations (delegate to connection)
// ============================================================================

bool http2_stream::SendHeaders(const std::vector<std::pair<std::string, std::string>> &headers,
                               bool end_stream) {
    if (!(_conn)) return false;
    return _conn->stream_send_headers(this, headers, end_stream);
}

bool http2_stream::SendData(const uint8_t *data, uint32_t size, bool end_stream) {
    if (!(_conn)) return false;
    return _conn->stream_send_data(this, data, size, end_stream);
}

bool http2_stream::SendTrailingHeaders(
    const std::vector<std::pair<std::string, std::string>> &headers) {
    if (!(_conn)) return false;
    return _conn->stream_send_trailing_headers(this, headers);
}

bool http2_stream::SendRSTStream(uint32_t error_code) {
    if (!(_conn)) return false;
    return _conn->stream_send_rst_stream(this, error_code);
}

// ============================================================================
// http2::Stream interface — data reading
// ============================================================================

uint32_t http2_stream::DataSize() const {
    return static_cast<uint32_t>(_data_cache.get_buffer_length());
}

uint32_t http2_stream::ReadData(uint8_t *buffer, uint32_t size) {
    return static_cast<uint32_t>(_data_cache.copy_to_buffer(buffer, size));
}

const uint8_t *http2_stream::PeekData(uint32_t *out_size) const {
    if (_data_cache.empty()) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    const slice &front = _data_cache.front();
    if (out_size) *out_size = static_cast<uint32_t>(front.size());
    return front.data();
}

// ============================================================================
// http2::Stream interface — header reading (lazy conversion)
// ============================================================================

const std::vector<std::pair<std::string, std::string>> &http2_stream::GetHeaders() const {
    if (_headers_dirty) {
        ensure_headers_decoded();
    }
    return _public_headers;
}

void http2_stream::ensure_headers_decoded() const {
    _public_headers.clear();
    for (auto &h : _headers) {
        _public_headers.push_back({h.key.to_string(), h.value.to_string()});
    }
    _headers_dirty = false;
}
