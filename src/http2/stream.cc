
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

const http2_stream::State event_status_table[static_cast<int>(
    _STREAM_EVENT_COUNTER)][static_cast<int>(http2_stream::ERROR)] = {
    {http2_stream::OPEN, http2_stream::ERROR, http2_stream::HALF_CLOSED_LOCAL, http2_stream::ERROR,
     http2_stream::ERROR, http2_stream::ERROR, http2_stream::ERROR},
    {http2_stream::OPEN, http2_stream::HALF_CLOSED_REMOTE, http2_stream::ERROR, http2_stream::ERROR,
     http2_stream::ERROR, http2_stream::ERROR, http2_stream::ERROR},
    {http2_stream::RESERVED_REMOTE, http2_stream::ERROR, http2_stream::ERROR, http2_stream::ERROR,
     http2_stream::ERROR, http2_stream::ERROR, http2_stream::ERROR},
    {http2_stream::RESERVED_LOCAL, http2_stream::ERROR, http2_stream::ERROR, http2_stream::ERROR,
     http2_stream::ERROR, http2_stream::ERROR, http2_stream::ERROR},
    {http2_stream::ERROR, http2_stream::ERROR, http2_stream::ERROR,
     http2_stream::HALF_CLOSED_REMOTE, http2_stream::CLOSED, http2_stream::ERROR,
     http2_stream::ERROR},
    {http2_stream::ERROR, http2_stream::ERROR, http2_stream::ERROR, http2_stream::HALF_CLOSED_LOCAL,
     http2_stream::ERROR, http2_stream::CLOSED, http2_stream::ERROR},
    {http2_stream::ERROR, http2_stream::CLOSED, http2_stream::CLOSED, http2_stream::CLOSED,
     http2_stream::CLOSED, http2_stream::CLOSED, http2_stream::ERROR},
    {http2_stream::ERROR, http2_stream::CLOSED, http2_stream::CLOSED, http2_stream::CLOSED,
     http2_stream::CLOSED, http2_stream::CLOSED, http2_stream::ERROR},
};

http2_stream::State get_next_status(http2_stream_event event, http2_stream::State status) {
    return event_status_table[event][status];
}

void http2_stream::send_push_promise() {
    _state = get_next_status(STREAM_EVENT_PP_S, _state);
}

void http2_stream::recv_push_promise() {
    _state = get_next_status(STREAM_EVENT_PP_R, _state);
}

void http2_stream::send_headers() {
    _state = get_next_status(STREAM_EVENT_H_S, _state);
}

void http2_stream::recv_headers(const std::vector<hpack::mdelem_data> &headers) {
    _finish_header = false;
    _state = get_next_status(STREAM_EVENT_H_R, _state);
    _headers.clear();
    _headers.resize(headers.size());
    for (size_t i = 0; i < headers.size(); i++) {
        _headers[i] = headers[i];
    }
}

void http2_stream::send_rst_stream() {
    _state = get_next_status(STREAM_EVENT_RST_S, _state);
}

void http2_stream::recv_rst_stream(uint32_t error_code) {
    _state = get_next_status(STREAM_EVENT_RST_R, _state);
    _last_error = error_code;
}

void http2_stream::send_end_stream() {
    _state = get_next_status(STREAM_EVENT_ES_S, _state);
    _sent_eos = true;
}

void http2_stream::recv_end_stream() {
    _state = get_next_status(STREAM_EVENT_ES_R, _state);
    _received_eos = true;
}

// ---------------------------------

http2_stream::http2_stream(ConnectionFlowControl *cfc, uint32_t stream_id)
    : _flow_control(stream_id, cfc)
    , _stream_id(stream_id)
    , _connection_id(cfc->ConnectionId())
    , _state(IDLE)
    , _frame_flags(0)
    , _frame_type(0)
    , _finish_header(false) {
    _read_closed = false;
    _write_closed = false;
    _received_eos = false;
    _sent_eos = false;
    _weight = 0;
    _last_error = 0;
}

uint8_t http2_stream::frame_type() {
    return _frame_type;
}

uint8_t http2_stream::frame_flags() {
    return _frame_flags;
}

void http2_stream::frame_type(uint8_t type) {
    _frame_type = type;
}

void http2_stream::frame_flags(uint8_t flags) {
    _frame_flags = flags;

    if (flags & HTTP2_FLAG_END_HEADERS) {
        _finish_header = true;
    }

    if (flags & HTTP2_FLAG_END_STREAM) {
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
    return _state == State::CLOSED;
}

uint32_t http2_stream::stream_id() const {
    return _stream_id;
}

void http2_stream::set_weight(int32_t w) {
    _weight = w;
}

http2_stream::State http2_stream::get_state() const {
    return _state;
}

void http2_stream::mark_unwritable() {
    _write_closed = true;
}

void http2_stream::mark_unreadable() {
    _read_closed = true;
}

bool http2_stream::try_send_push_promise() {
    if (_write_closed) return false;
    auto s = get_next_status(STREAM_EVENT_PP_S, _state);
    return (s != State::ERROR);
}

bool http2_stream::try_send_headers() {
    if (_write_closed) return false;
    auto s = get_next_status(STREAM_EVENT_H_S, _state);
    return (s != State::ERROR);
}

bool http2_stream::try_send_rst_stream() {
    if (_write_closed) return false;
    auto s = get_next_status(STREAM_EVENT_RST_S, _state);
    return (s != State::ERROR);
}

bool http2_stream::try_send_end_stream() {
    if (_write_closed) return false;
    auto s = get_next_status(STREAM_EVENT_ES_S, _state);
    return (s != State::ERROR);
}

std::shared_ptr<http2::Stream> http2_stream::get_shared_stream() {
    return shared_from_this();
}
