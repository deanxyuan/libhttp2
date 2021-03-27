
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
       | send R /  ‘----------->|        |<-----------’  send R / |
       | recv R                 | closed |               recv R   |
       ‘----------------------->|        |<----------------------’
                                +--------+
          send:   endpoint sends this frame
          recv:   endpoint receives this frame
          H:  HEADERS frame (with implied CONTINUATIONs)
          PP: PUSH_PROMISE frame (with implied CONTINUATIONs)
          ES: END_STREAM flag
          R:  RST_STREAM frame
*/

#include "src/http2/stream.h"
#include <assert.h>
#include "src/http2/frame.h"

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
constexpr int kIdle = HTTP2_STREAM_IDLE;
constexpr int kReservedLocal = HTTP2_STREAM_RESERVED_LOCAL;
constexpr int kReservedRemote = HTTP2_STREAM_RESERVED_LOCAL;
constexpr int kOpen = HTTP2_STREAM_OPEN;
constexpr int kHalfClosedLocal = HTTP2_STREAM_HALF_CLOSED_LOCAL;
constexpr int kHalfClosedRemote = HTTP2_STREAM_HALF_CLOSED_REMOTE;
constexpr int kClosed = HTTP2_STREAM_CLOSED;
constexpr int kDoNotUse = kClosed + 1;
}  // namespace internal

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

static inline int get_next_status(http2_stream_event event, int current) {
    return event_status_table[static_cast<int>(event)][current];
}

void http2_stream::send_push_promise() {
    auto next_state = get_next_status(STREAM_EVENT_PP_S, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
}

void http2_stream::recv_push_promise() {
    auto next_state = get_next_status(STREAM_EVENT_PP_R, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
}

void http2_stream::send_headers() {
    auto next_state = get_next_status(STREAM_EVENT_H_S, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
}

void http2_stream::recv_headers(std::vector<hpack::mdelem_data> &headers) {
    auto next_state = get_next_status(STREAM_EVENT_H_R, _state);
    if (next_state != internal::kDoNotUse) {
        _state = next_state;
    }
    _headers = std::move(headers);
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

// ---------------------------------

http2_stream::http2_stream(uint64_t connection_id, uint32_t stream_id)
    : _connection_id(connection_id)
    , _stream_id(stream_id)
    , _state(internal::kIdle)
    , _frame_flags(0)
    , _frame_type(0) {
    _read_closed = false;
    _write_closed = false;
    _spec.depend_stream_id = 0;
    _spec.exclusive = false;
    _spec.weight = 16;
    _last_error = 0;
}

uint8_t http2_stream::frame_type() {
    return _frame_type;
}

uint8_t http2_stream::frame_flags() {
    return _frame_flags;
}

void http2_stream::save_frame_info(http2_frame_hdr *hdr) {
    _frame_type = hdr->type;
    _frame_flags = hdr->flags;
    if (_frame_flags & HTTP2_FLAG_END_STREAM) {
        recv_end_stream();
    }
}

void http2_stream::append_headers(const std::vector<hpack::mdelem_data> &headers) {
    for (size_t i = 0; i < headers.size(); i++) {
        _headers.emplace_back(headers[i]);
    }
}

void http2_stream::append_data(slice s) {
    slice obj(s.data(), s.size());
    _data_cache.add_slice(obj);
}

bool http2_stream::is_closed() const {
    return _state == internal::kClosed;
}

uint32_t http2_stream::stream_id() const {
    return _stream_id;
}

void http2_stream::set_priority_info(http2_priority_spec *info) {
    _spec = *info;
}

int http2_stream::get_state() const {
    return _state;
}

void http2_stream::mark_unwritable() {
    _write_closed = true;
}

void http2_stream::mark_unreadable() {
    _read_closed = true;
}

std::shared_ptr<http2::Stream> http2_stream::get_shared_stream() {
    return shared_from_this();
}

uint64_t http2_stream::ConnectionId() {
    return _connection_id;
}

uint32_t http2_stream::StreamId() {
    return _stream_id;
}

int32_t http2_stream::Weight() {
    return _spec.weight;
}

bool http2_stream::Exclusive() {
    return _spec.exclusive;
}

int32_t http2_stream::Flags() {
    return _frame_flags;
}

uint32_t http2_stream::ErrorCode() {
    return _last_error;
}

int http2_stream::CurrentState() {
    return _state;
}

std::multimap<std::string, std::string> http2_stream::GetHeaders() {
    std::multimap<std::string, std::string> headers;
    for (size_t i = 0; i < _headers.size(); i++) {
        std::string k = _headers[i].key.to_string();
        std::string v = _headers[i].value.to_string();
        headers.insert({k, v});
    }
    return headers;
}

uint32_t http2_stream::GetDataBlock(uint32_t (*parse_func)(const uint8_t *ptr, uint32_t len)) {
    if (parse_func) {
        slice s = _data_cache.merge();
        return parse_func(s.data(), s.size());
    }
    return static_cast<uint32_t>(_data_cache.get_buffer_length());
}

void http2_stream::PopDataBlock(uint8_t *output, uint32_t size) {
    assert(size <= _data_cache.get_buffer_length());
    _data_cache.copy_to_buffer(output, size);
    _data_cache.move_header(size);
}
