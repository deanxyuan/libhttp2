/**
 * @file connection.cc
 * @brief HTTP/2 connection implementation -- frame dispatch, settings handling,
 *        stream management, and send/receive logic.
 */

#include "src/http2/connection.h"
#include <string.h>
#include "src/http2/stream.h"
#include "src/http2/errors.h"
#include "src/http2/frame.h"
#include "src/http2/parser.h"
#include "src/hpack/hpack.h"
#include <assert.h>
#include "src/http2/pack.h"

using http2::global_settings_parameters;

const uint8_t http2_connection::PREFACE[24] = {0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54,
                                               0x54, 0x50, 0x2f, 0x32, 0x2e, 0x30, 0x0d, 0x0a,
                                               0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a};

typedef std::shared_ptr<http2_stream> (*internal_forward_func)(http2_connection *,
                                                               http2_frame_hdr *, const uint8_t *);

static std::shared_ptr<http2_stream>
internal_data_process(http2_connection *conn, http2_frame_hdr *hdr, const uint8_t *package) {
    std::shared_ptr<http2_stream> stream;
    if (hdr->stream_id > 0) {
        stream = conn->find_stream(hdr->stream_id);
    }
    http2_frame_data frame;
    parse_http2_frame_data(hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
    conn->received_data(stream, &frame);
    return stream;
}

static std::shared_ptr<http2_stream>
internal_headers_process(http2_connection *conn, http2_frame_hdr *hdr, const uint8_t *package) {
    std::shared_ptr<http2_stream> stream;
    if (hdr->stream_id > 0) {
        stream = conn->find_stream(hdr->stream_id);
    }
    http2_frame_headers frame;
    parse_http2_frame_headers(hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
    conn->received_headers(stream, &frame);
    return stream;
}

static std::shared_ptr<http2_stream>
internal_priority_process(http2_connection *conn, http2_frame_hdr *hdr, const uint8_t *package) {
    std::shared_ptr<http2_stream> stream;
    if (hdr->stream_id > 0) {
        stream = conn->find_stream(hdr->stream_id);
    }
    http2_frame_priority frame;
    parse_http2_frame_priority(hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
    conn->received_priority(stream, &frame);
    return stream;
}

static std::shared_ptr<http2_stream>
internal_rst_stream_process(http2_connection *conn, http2_frame_hdr *hdr, const uint8_t *package) {
    std::shared_ptr<http2_stream> stream;
    if (hdr->stream_id > 0) {
        stream = conn->find_stream(hdr->stream_id);
    }
    http2_frame_rst_stream frame;
    parse_http2_frame_rst_stream(hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
    conn->received_rst_stream(stream, &frame);
    return stream;
}

static std::shared_ptr<http2_stream>
internal_settings_process(http2_connection *conn, http2_frame_hdr *hdr, const uint8_t *package) {
    http2_frame_settings frame;
    parse_http2_frame_settings(hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
    conn->received_settings(&frame);
    return nullptr;
}

static std::shared_ptr<http2_stream> internal_push_promise_process(http2_connection *conn,
                                                                   http2_frame_hdr *hdr,
                                                                   const uint8_t *package) {
    std::shared_ptr<http2_stream> stream;
    if (hdr->stream_id > 0) {
        stream = conn->find_stream(hdr->stream_id);
    }
    http2_frame_push_promise frame;
    parse_http2_frame_push_promise(hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
    conn->received_push_promise(stream, &frame);
    return stream;
}

static std::shared_ptr<http2_stream>
internal_ping_process(http2_connection *conn, http2_frame_hdr *hdr, const uint8_t *package) {
    http2_frame_ping frame;
    parse_http2_frame_ping(hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
    conn->received_ping(&frame);
    return nullptr;
}

static std::shared_ptr<http2_stream>
internal_goaway_process(http2_connection *conn, http2_frame_hdr *hdr, const uint8_t *package) {
    http2_frame_goaway frame;
    parse_http2_frame_goaway(hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
    conn->received_goaway(&frame);
    return nullptr;
}

static std::shared_ptr<http2_stream> internal_window_update_process(http2_connection *conn,
                                                                    http2_frame_hdr *hdr,
                                                                    const uint8_t *package) {
    std::shared_ptr<http2_stream> stream;
    if (hdr->stream_id > 0) {
        stream = conn->find_stream(hdr->stream_id);
    }
    http2_frame_window_update frame;
    parse_http2_frame_window_update(hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
    conn->received_window_update(stream, &frame);
    return stream;
}

static std::shared_ptr<http2_stream> internal_continuation_process(http2_connection *conn,
                                                                   http2_frame_hdr *hdr,
                                                                   const uint8_t *package) {
    std::shared_ptr<http2_stream> stream;
    if (hdr->stream_id > 0) {
        stream = conn->find_stream(hdr->stream_id);
    }
    http2_frame_continuation frame;
    parse_http2_frame_continuation(hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
    conn->received_continuation(stream, &frame);
    return stream;
}

static internal_forward_func frame_process_func_array[10] = {
    internal_data_process,         internal_headers_process,  internal_priority_process,
    internal_rst_stream_process,   internal_settings_process, internal_push_promise_process,
    internal_ping_process,         internal_goaway_process,   internal_window_update_process,
    internal_continuation_process,
};

http2_connection::http2_connection(http2::SendService *sender, uint64_t cid, bool client_side)
    : _dynamic_table(global_settings_parameters[static_cast<size_t>(Http2SettingsId::HeaderTableSize)].default_value)
    , _sender_service(sender)
    , _event_handler(nullptr)
    , _flow_control(nullptr)
    , _connection_id(cid)
    , _client_side(client_side)
    , _ping_pending(false)
    , _settings_pending(false)
    , _preface_sent(false)
    , _initial_settings_sent(false) {

    for (size_t i = 0; i < HTTP2_NUMBER_OF_SETTINGS; i++) {
        _local_settings[i] = global_settings_parameters[i].default_value;
        _remote_settings[i] = global_settings_parameters[i].default_value;
    }

    _local_settings[static_cast<size_t>(Http2SettingsId::InitialWindowSize)] = INITIAL_WINDOW_SIZE;
    _local_settings[static_cast<size_t>(Http2SettingsId::MaxFrameSize)] = MAX_FRAME_SIZE;
    _local_settings[static_cast<size_t>(Http2SettingsId::MaxHeaderListSize)] = MAX_HEADER_LIST_SIZE;

    _next_local_stream_id = client_side ? 1 : 2;

    hpack::compressor_init(&_send_record);
    hpack::compressor_set_max_table_size(&_send_record,
                                         _remote_settings[static_cast<size_t>(Http2SettingsId::HeaderTableSize)]);

    _finish_handshake = client_side;  // only server needs to verify client preface
    _last_stream_id = 0;

    _next_frame_limit = false;
    _next_stream_id_limit = 0;
    _received_goaway_stream_id = 0;
    _received_goaway = false;
    _sent_goaway_stream_id = 0;
    _sent_goaway = false;
    _draining = false;

    _buffered_mode = false;
}

http2_connection::~http2_connection() {
    hpack::compressor_destroy(&_send_record);
}

std::shared_ptr<http2_stream> http2_connection::create_stream() {
    if (_next_local_stream_id >= HTTP2_MAX_STREAM_ID || _received_goaway || _sent_goaway) {
        return nullptr;
    }
    auto stream = std::make_shared<http2_stream>(_connection_id, _next_local_stream_id, this);
    _streams[_next_local_stream_id] = stream;
    _next_local_stream_id += 2;
    return stream;
}

void http2_connection::destroy_stream(uint32_t stream_id) {
    _streams.erase(stream_id);
}

std::shared_ptr<http2_stream> http2_connection::find_stream(uint32_t stream_id) {
    auto it = _streams.find(stream_id);
    if (it == _streams.end()) {
        return nullptr;
    }
    return it->second;
}

http2::ConnectionInfo http2_connection::get_connection_info() const {
    http2::ConnectionInfo info{};
    info.active_streams = static_cast<uint32_t>(_streams.size());
    info.last_stream_id = _last_stream_id;
    info.received_goaway = _received_goaway;
    info.sent_goaway = _sent_goaway;
    info.draining = _draining;
    info.connection_window = INITIAL_WINDOW_SIZE;
    return info;
}

uint64_t http2_connection::connection_id() const {
    return _connection_id;
}

bool http2_connection::is_client_side() const {
    return _client_side;
}

bool http2_connection::need_verify_preface() const {
    return !_finish_handshake;
}

void http2_connection::verify_preface_done() {
    _finish_handshake = true;
}

void http2_connection::ensure_preface_sent() {
    if (!_client_side || _preface_sent) return;

    // 1. Send the 24-byte connection preface
    int ret = _sender_service->SendRawData(_connection_id, PREFACE, PREFACE_SIZE);
    if (ret < 0) return;

    // 2. Build and send a default SETTINGS frame with non-default values
    std::vector<http2_settings_entry> vse;
    for (size_t i = 1; i < HTTP2_NUMBER_OF_SETTINGS; i++) {
        if (_local_settings[i] != global_settings_parameters[i].default_value) {
            http2_settings_entry entry;
            entry.id = static_cast<uint16_t>(i);
            entry.value = _local_settings[i];
            vse.push_back(entry);
        }
    }
    http2_frame_settings frame = build_http2_frame_settings(0, &vse);
    send_http2_frame(&frame);

    _settings_pending = true;
    _preface_sent = true;
}

void http2_connection::send_initial_settings() {
    if (_initial_settings_sent) return;
    _initial_settings_sent = true;

    // Build a SETTINGS frame with any non-default local settings
    std::vector<http2_settings_entry> vse;
    for (size_t i = 1; i < HTTP2_NUMBER_OF_SETTINGS; i++) {
        if (_local_settings[i] != global_settings_parameters[i].default_value) {
            http2_settings_entry entry;
            entry.id = static_cast<uint16_t>(i);
            entry.value = _local_settings[i];
            vse.push_back(entry);
        }
    }
    http2_frame_settings frame = build_http2_frame_settings(0, &vse);
    send_http2_frame(&frame);
    _settings_pending = true;  // wait for ACK
}

void http2_connection::set_event_handler(http2::EventHandler *h) {
    _event_handler = h;
}

void http2_connection::set_flow_control_handler(http2::FlowControlHandler *h) {
    _flow_control = h;
}

bool http2_connection::send_ping(uint64_t info) {
    ensure_preface_sent();
    if (_ping_pending) {
        return false;
    }
    uint8_t data[8] = {0};
    memcpy(data, &info, 8);
    auto frame = build_http2_frame_ping(data, false);
    send_http2_frame(&frame);
    _ping_pending = true;
    return true;
}

bool http2_connection::send_settings(const std::vector<std::pair<uint16_t, uint32_t>> &settings) {
    ensure_preface_sent();
    if (settings.empty()) return false;
    if (_settings_pending) return false;

    _settings_pending = true;
    _request_settings.clear();

    std::vector<http2_settings_entry> vse;
    for (size_t i = 0; i < settings.size(); i++) {
        http2_settings_entry entry;
        entry.id = settings[i].first;
        entry.value = settings[i].second;
        vse.push_back(entry);

        _request_settings.push_back(settings[i]);
    }

    http2_frame_settings frame = build_http2_frame_settings(0, &vse);
    send_http2_frame(&frame);
    return true;
}

bool http2_connection::send_rst_stream(uint32_t stream_id, uint32_t error_code) {
    auto stream = find_stream(stream_id);
    if (!stream) return false;
    http2_frame_rst_stream frame = build_http2_frame_rst_stream(stream_id, 0, error_code);
    send_http2_frame(&frame);
    stream->send_rst_stream();
    if (stream->is_closed()) {
        _streams.erase(stream_id);
        check_drain_complete();
    }
    return true;
}

bool http2_connection::send_goaway(uint32_t error_code, uint32_t last_stream_id,
                                   const std::string &debug) {
    if (last_stream_id == 0) {
        last_stream_id = _last_stream_id;
    }
    http2_frame_goaway frame = build_http2_frame_goaway(last_stream_id, error_code, debug);
    send_http2_frame(&frame);

    _sent_goaway_stream_id = last_stream_id;
    _sent_goaway = true;
    return true;
}

void http2_connection::drain() {
    if (_sent_goaway) return;
    send_goaway(static_cast<uint32_t>(Http2ErrorCode::NoError), _last_stream_id);
    _draining = true;
    check_drain_complete();
}

void http2_connection::check_drain_complete() {
    if (_draining && _streams.empty()) {
        _draining = false;
        if (_event_handler) {
            _event_handler->OnShutdownComplete(_connection_id);
        }
    }
}

std::shared_ptr<http2_stream> http2_connection::send_push_promise(
    http2_stream *request_stream,
    const std::vector<std::pair<std::string, std::string>> &headers) {
    if (_client_side) return nullptr;  // only server can push
    if (!_local_settings[static_cast<size_t>(Http2SettingsId::EnablePush)]) return nullptr;

    auto promised = create_stream();
    if (!promised) return nullptr;

    // HPACK encode the headers
    std::vector<hpack::mdelem_data> mdels;
    for (size_t i = 0; i < headers.size(); i++) {
        mdels.push_back({headers[i].first, headers[i].second});
    }
    bool use_true_binary = true;
    slice_buffer header_block_fragment;
    hpack::compressor_encode_headers(&_send_record, &mdels, &header_block_fragment, use_true_binary);

    int flags = static_cast<uint8_t>(Http2FrameFlag::EndHeaders);
    http2_frame_push_promise frame = build_http2_frame_push_promise(
        request_stream->stream_id(), promised->stream_id(), flags,
        header_block_fragment.merge());

    send_http2_frame(&frame);
    promised->send_push_promise();
    return promised;
}

bool http2_connection::send_window_update(uint32_t stream_id, const http2::WindowUpdate &wu) {
    ensure_preface_sent();
    uint32_t conn_inc = wu.connection_window_size_increment & 0x7fffffff;
    uint32_t stream_inc = wu.stream_window_size_increment & 0x7fffffff;

    if (stream_inc > 0) {
        auto stream = find_stream(stream_id);
        if (!stream) {
            stream_inc = 0;
        }
    }

    slice s;
    if (stream_inc > 0) {
        http2_frame_window_update frame =
            build_http2_frame_window_update(stream_id, stream_inc);
        s += pack_http2_frame_window_update(&frame);
    }
    if (conn_inc > 0) {
        http2_frame_window_update frame =
            build_http2_frame_window_update(0, conn_inc);
        s += pack_http2_frame_window_update(&frame);
    }
    send_raw_data(s);
    return !s.empty();
}

// === Stream-level send operations (called by http2_stream) ===

bool http2_connection::stream_send_headers(http2_stream *stream,
    const std::vector<std::pair<std::string, std::string>> &headers, bool end_stream) {
    ensure_preface_sent();
    auto s = find_stream(stream->stream_id());
    if (!s) return false;
    int flags = static_cast<uint8_t>(Http2FrameFlag::EndHeaders);
    if (end_stream) flags |= static_cast<uint8_t>(Http2FrameFlag::EndStream);
    send_binary_in_headers_frame(s, headers, flags);
    stream->send_headers();
    if (end_stream) {
        stream->send_end_stream();
    }
    if (stream->is_closed()) {
        destroy_stream(stream->stream_id());
        check_drain_complete();
    }
    return true;
}

bool http2_connection::stream_send_data(http2_stream *stream,
    const uint8_t *data, uint32_t size, bool end_stream) {
    ensure_preface_sent();
    auto s = find_stream(stream->stream_id());
    if (!s) return false;
    send_binary_in_data_frame(s, data, size, end_stream);
    if (end_stream) stream->send_end_stream();
    if (stream->is_closed()) {
        destroy_stream(stream->stream_id());
        check_drain_complete();
    }
    return true;
}

bool http2_connection::stream_send_trailing_headers(http2_stream *stream,
    const std::vector<std::pair<std::string, std::string>> &headers) {
    ensure_preface_sent();
    auto s = find_stream(stream->stream_id());
    if (!s) return false;
    int flags = static_cast<uint8_t>(Http2FrameFlag::EndHeaders) |
                static_cast<uint8_t>(Http2FrameFlag::EndStream);
    send_binary_in_headers_frame(s, headers, flags);
    stream->send_headers();
    stream->send_end_stream();
    if (stream->is_closed()) {
        destroy_stream(stream->stream_id());
        check_drain_complete();
    }
    return true;
}

bool http2_connection::stream_send_rst_stream(http2_stream *stream, uint32_t error_code) {
    return send_rst_stream(stream->stream_id(), error_code);
}

void http2_connection::send_binary_in_headers_frame(
    std::shared_ptr<http2_stream> &stream,
    const std::vector<std::pair<std::string, std::string>> &headers, int flags) {

    std::vector<hpack::mdelem_data> mdels;
    for (size_t i = 0; i < headers.size(); i++) {
        mdels.push_back({headers[i].first, headers[i].second});
    }

    bool use_true_binary_metadata = 1;
    slice_buffer header_block_fragment;
    hpack::compressor_encode_headers(&_send_record, &mdels, &header_block_fragment,
                                     use_true_binary_metadata);

    http2_frame_headers frame = build_http2_frame_headers(stream->stream_id(), flags,
                                                          header_block_fragment.merge(), nullptr);
    send_http2_frame(&frame);
}

void http2_connection::send_binary_in_data_frame(std::shared_ptr<http2_stream> &stream,
                                                 const uint8_t *data, uint32_t size,
                                                 bool end_of_stream) {
    if (size == 0) return;
    if (_flow_control) {
        http2::WindowUpdate wu =
            _flow_control->OnPreSendData(_connection_id, stream->stream_id(), size);
        send_window_update(stream->stream_id(), wu);
    }
    slice d(data, size);
    int flags = end_of_stream ? static_cast<uint8_t>(Http2FrameFlag::EndStream) : 0;
    http2_frame_data frame = build_http2_frame_data(stream->stream_id(), flags, d);
    send_http2_frame(&frame);
}

int http2_connection::package_process(const uint8_t *package, uint32_t /*package_length*/) {
    assert(package_length >= HTTP2_FRAME_HEADER_SIZE);

    http2_frame_hdr hdr;
    http2_frame_header_unpack(&hdr, package);

    assert(package_length >= hdr.length + HTTP2_FRAME_HEADER_SIZE);

    // check END_HEADERS limit
    if (_next_frame_limit) {
        if (hdr.type != static_cast<uint8_t>(Http2FrameType::Continuation) || hdr.stream_id != _next_stream_id_limit) {
            send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
            return -1;
        }
    }
    //
    // TODO: when _sent_goaway = true
    //

    // Only track remote-initiated stream IDs
    if (hdr.stream_id > 0) {
        bool is_remote_stream = _client_side ? !(hdr.stream_id & 1) : (hdr.stream_id & 1);
        if (is_remote_stream && hdr.stream_id > _last_stream_id) {
            _last_stream_id = hdr.stream_id;
        }
    }

    if (hdr.type > static_cast<uint8_t>(Http2FrameType::Continuation)) {
        return static_cast<int>(hdr.length + HTTP2_FRAME_HEADER_SIZE);
    }

    std::shared_ptr<http2_stream> stream = frame_process_func_array[hdr.type](this, &hdr, package);

    if (stream && stream->is_closed()) {
        destroy_stream(stream->stream_id());
        check_drain_complete();
    }
    return static_cast<int>(hdr.length + HTTP2_FRAME_HEADER_SIZE);
}

void http2_connection::received_data(std::shared_ptr<http2_stream> &stream,
                                     http2_frame_data *frame) {
    if (frame->pad_len >= frame->hdr.length) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    if (frame->data.empty()) return;
    if (!stream) return;

    stream->append_data(frame->data);
    stream->save_frame_info(&frame->hdr);

    if (_event_handler) {
        _event_handler->OnStreamData(stream->get_shared_stream());
    }

    if (_flow_control) {
        http2::WindowUpdate wu =
            _flow_control->OnDataReceived(_connection_id, stream->stream_id(),
                                          static_cast<uint32_t>(frame->data.size()));
        send_window_update(stream->stream_id(), wu);
    } else {
        // Automatic flow control: send WINDOW_UPDATE for received data
        http2::WindowUpdate wu = {static_cast<uint32_t>(frame->data.size()),
                                  static_cast<uint32_t>(frame->data.size())};
        send_window_update(stream->stream_id(), wu);
    }

    if (stream->is_closed()) {
        notify_stream_closed(stream, 0);
    }
}

void http2_connection::received_headers(std::shared_ptr<http2_stream> &stream,
                                        http2_frame_headers *frame) {
    if (frame->hdr.stream_id == 0) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    if (frame->pad_len >= frame->hdr.length) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    if (_client_side) {
        if (frame->hdr.stream_id & 1) {
            send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
            return;
        }
    } else {
        if (!(frame->hdr.stream_id & 1)) {
            send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
            return;
        }
    }

    slice headers = frame->header_block_fragment;
    std::vector<hpack::mdelem_data> decoded_headers;
    int err =
        hpack::decode_headers(headers.data(), static_cast<uint32_t>(headers.size()), &_dynamic_table, &decoded_headers);
    if (err != static_cast<uint32_t>(Http2ErrorCode::NoError)) {
        send_goaway(err);
        return;
    }

    if (!stream) {
        // RFC 7540 Section6.8: do not process streams above GOAWAY last_stream_id
        if (_sent_goaway && frame->hdr.stream_id > _sent_goaway_stream_id) {
            return;
        }
        stream = std::make_shared<http2_stream>(_connection_id, frame->hdr.stream_id, this);
        _streams[frame->hdr.stream_id] = stream;
    }

    stream->save_frame_info(&frame->hdr);
    stream->recv_headers(decoded_headers);

    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::EndHeaders)) {
        _next_frame_limit = false;
        _next_stream_id_limit = 0;
        if (_event_handler) {
            _event_handler->OnStreamHeaders(stream->get_shared_stream());
        }
    } else {
        _next_frame_limit = true;
        _next_stream_id_limit = frame->hdr.stream_id;
    }
}

void http2_connection::received_priority(std::shared_ptr<http2_stream> &stream,
                                         http2_frame_priority *frame) {
    if (frame->hdr.length != 5) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::FrameSizeError));
        return;
    }
    if (frame->hdr.stream_id == 0) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    // A stream cannot depend on itself
    if (frame->hdr.stream_id == frame->pspec.depend_stream_id) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    if (stream) {
        stream->set_priority_info(&frame->pspec);
        stream->save_frame_info(&frame->hdr);
    }
}

void http2_connection::received_rst_stream(std::shared_ptr<http2_stream> &stream,
                                           http2_frame_rst_stream *frame) {
    if (frame->hdr.length != 4) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::FrameSizeError));
        return;
    }

    if (stream) {
        stream->recv_rst_stream(frame->error_code);
        stream->save_frame_info(&frame->hdr);
        notify_stream_closed(stream, frame->error_code);
    }
}

void http2_connection::received_settings(http2_frame_settings *frame) {
    if (frame->hdr.length % 6) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::FrameSizeError));
        return;
    }

    if (frame->hdr.stream_id) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::Ack)) {
        if (frame->hdr.length != 0) {
            send_goaway(static_cast<uint32_t>(Http2ErrorCode::FrameSizeError));
            return;
        }
        for (size_t i = 0; i < _request_settings.size(); i++) {
            uint16_t setting_id = _request_settings[i].first;
            uint32_t value = _request_settings[i].second;
            if (setting_id < HTTP2_NUMBER_OF_SETTINGS) {
                _local_settings[setting_id] = value;
            }
        }
        _settings_pending = false;
        _request_settings.clear();

        if (_event_handler) {
            _event_handler->OnSettings(_connection_id, {}, true);
        }
        return;
    }

    std::vector<std::pair<uint16_t, uint32_t>> settings_vec;
    for (size_t i = 0; i < frame->settings.size(); i++) {
        http2_settings_entry setting = frame->settings[i];
        if (setting.id < HTTP2_NUMBER_OF_SETTINGS) {
            const auto &params = http2::global_settings_parameters[setting.id];
            if (setting.value < params.min_value || setting.value > params.max_value) {
                if (params.invalid_value_behavior == InvalidValueBehavior::Disconnect) {
                    send_goaway(static_cast<uint32_t>(params.error_value));
                    return;
                }
                // Clamp
                if (setting.value < params.min_value) setting.value = params.min_value;
                if (setting.value > params.max_value) setting.value = params.max_value;
            }
            _remote_settings[setting.id] = setting.value;
            if (setting.id == static_cast<size_t>(Http2SettingsId::HeaderTableSize)) {
                hpack::compressor_set_max_table_size(&_send_record, setting.value);
            }
        }
        settings_vec.push_back({setting.id, setting.value});
    }

    if (_event_handler) {
        _event_handler->OnSettings(_connection_id, settings_vec, false);
    }

    http2_frame_settings settings_ack = build_http2_frame_settings(static_cast<uint8_t>(Http2FrameFlag::Ack), nullptr);
    send_http2_frame(&settings_ack);
}

void http2_connection::received_push_promise(std::shared_ptr<http2_stream> & /*stream*/,
                                             http2_frame_push_promise *frame) {
    if (frame->pad_len >= frame->hdr.length) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }
    if (frame->promised_stream_id == 0) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    if (!_local_settings[static_cast<size_t>(Http2SettingsId::EnablePush)]) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    auto promise_stream = find_stream(frame->promised_stream_id);
    if (!promise_stream) {
        promise_stream = std::make_shared<http2_stream>(_connection_id, frame->promised_stream_id, this);
    } else if (promise_stream->get_state() != static_cast<int>(Http2StreamState::Idle)) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    std::vector<hpack::mdelem_data> decoded_headers;
    slice headers = frame->header_block_fragment;
    int ret =
        hpack::decode_headers(headers.data(), static_cast<uint32_t>(headers.size()), &_dynamic_table, &decoded_headers);
    if (ret != static_cast<uint32_t>(Http2ErrorCode::NoError)) {
        send_goaway(ret);
        return;
    }

    promise_stream->recv_push_promise();
    promise_stream->save_frame_info(&frame->hdr);
    promise_stream->append_headers(decoded_headers);

    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::EndHeaders)) {
        _next_frame_limit = false;
        _next_stream_id_limit = 0;
        if (_event_handler) {
            _event_handler->OnStreamHeaders(promise_stream->get_shared_stream());
        }
    } else {
        _next_frame_limit = true;
        _next_stream_id_limit = frame->promised_stream_id;
    }
}

void http2_connection::received_ping(http2_frame_ping *frame) {
    if (frame->hdr.length != 8) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::FrameSizeError));
        return;
    }
    if (frame->hdr.stream_id) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    uint64_t ping_data;
    memcpy(&ping_data, frame->opaque_data, 8);
    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::Ack)) {
        _ping_pending = false;
        if (_event_handler) {
            _event_handler->OnPing(_connection_id, ping_data, true);
        }
    } else {
        if (_event_handler) {
            _event_handler->OnPing(_connection_id, ping_data, false);
        }
        http2_frame_ping ping_ack = build_http2_frame_ping(frame->opaque_data, true);
        send_http2_frame(&ping_ack);
    }
}

void http2_connection::received_goaway(http2_frame_goaway *frame) {
    if (frame->hdr.stream_id) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }
    _received_goaway_stream_id = frame->last_stream_id;
    _received_goaway = true;

    // stream ID greater than _goaway_stream_id can still send data
    for (auto it = _streams.begin(); it != _streams.end(); ++it) {
        if (_client_side) {
            if (it->first & 1 && it->first <= _received_goaway_stream_id) {
                it->second->mark_unwritable();
            }
        } else {
            if (it->first % 2 == 0 && it->first <= _received_goaway_stream_id) {
                it->second->mark_unwritable();
            }
        }
    }
    if (_event_handler) {
        _event_handler->OnGoAway(_connection_id, frame->last_stream_id, frame->error_code,
                                 frame->debug_data.to_string());
    }
}

void http2_connection::received_window_update(std::shared_ptr<http2_stream> &stream,
                                              http2_frame_window_update *frame) {
    if (frame->hdr.length != 4) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::FrameSizeError));
        return;
    }
    if (frame->window_size_inc < 1 || frame->window_size_inc > 2147483647) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    if (!_flow_control) return;
    if (!stream) {
        _flow_control->OnWindowUpdate(_connection_id, 0, frame->window_size_inc);
        return;
    }
    stream->save_frame_info(&frame->hdr);
    _flow_control->OnWindowUpdate(_connection_id, frame->hdr.stream_id, frame->window_size_inc);
}

void http2_connection::received_continuation(std::shared_ptr<http2_stream> &stream,
                                             http2_frame_continuation *frame) {
    if (!_next_frame_limit) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    if (!stream) {
        send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
        return;
    }

    slice headers = frame->header_block_fragment;
    std::vector<hpack::mdelem_data> decoded_headers;
    int ret =
        hpack::decode_headers(headers.data(), static_cast<uint32_t>(headers.size()), &_dynamic_table, &decoded_headers);
    if (ret != static_cast<uint32_t>(Http2ErrorCode::NoError)) {
        send_goaway(ret);
        return;
    }

    stream->append_headers(decoded_headers);
    stream->save_frame_info(&frame->hdr);

    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::EndHeaders)) {
        _next_frame_limit = false;
        _next_stream_id_limit = 0;
        if (_event_handler) {
            _event_handler->OnStreamHeaders(stream->get_shared_stream());
        }
    }
}

void http2_connection::notify_stream_closed(std::shared_ptr<http2_stream> &stream,
                                            uint32_t error_code) {
    if (_event_handler) {
        _event_handler->OnStreamClosed(stream->get_shared_stream(), error_code);
    }
}

int http2_connection::send_raw_data(const slice_buffer &sb) {
    if (_buffered_mode) {
        buffer_raw_data(sb);
        return static_cast<int>(sb.get_buffer_length());
    }
    int total = 0;
    for (size_t i = 0; i < sb.slice_count(); i++) {
        const slice &s = sb[i];
        int ret = _sender_service->SendRawData(_connection_id, s.data(), static_cast<uint32_t>(s.size()));
        if (ret < 0) return -1;
        total += ret;
    }
    return total;
}

int http2_connection::send_raw_data(slice s) {
    if (_buffered_mode) {
        buffer_raw_data(s);
        return static_cast<int>(s.size());
    }
    return _sender_service->SendRawData(_connection_id, s.data(), static_cast<uint32_t>(s.size()));
}

void http2_connection::set_buffered_mode(bool enable) {
    _buffered_mode = enable;
}

void http2_connection::buffer_raw_data(slice s) {
    _send_buffer.add_slice(std::move(s));
}

void http2_connection::buffer_raw_data(const slice_buffer &sb) {
    for (size_t i = 0; i < sb.slice_count(); i++) {
        _send_buffer.add_slice(sb[i]);
    }
}

bool http2_connection::flush_buffer() {
    if (_send_buffer.empty()) return true;
    // Temporarily disable buffered mode so send_raw_data actually sends.
    bool prev = _buffered_mode;
    _buffered_mode = false;
    int ret = send_raw_data(_send_buffer);
    _send_buffer.clear_buffer();
    _buffered_mode = prev;
    return ret >= 0;
}

void http2_connection::send_http2_frame(http2_frame_data *frame) {
    slice_buffer sb = pack_http2_frame_data(frame, local_settings(static_cast<size_t>(Http2SettingsId::MaxFrameSize)));
    send_raw_data(sb);
}
void http2_connection::send_http2_frame(http2_frame_headers *frame) {
    slice s = pack_http2_frame_headers(frame);
    send_raw_data(s);
}
void http2_connection::send_http2_frame(http2_frame_priority *frame) {
    slice s = pack_http2_frame_priority(frame);
    send_raw_data(s);
}
void http2_connection::send_http2_frame(http2_frame_rst_stream *frame) {
    slice s = pack_http2_frame_rst_stream(frame);
    send_raw_data(s);
}
void http2_connection::send_http2_frame(http2_frame_settings *frame) {
    slice s = pack_http2_frame_settings(frame);
    send_raw_data(s);
}
void http2_connection::send_http2_frame(http2_frame_push_promise *frame) {
    slice s = pack_http2_frame_push_promise(frame);
    send_raw_data(s);
}
void http2_connection::send_http2_frame(http2_frame_ping *frame) {
    slice s = pack_http2_frame_ping(frame);
    send_raw_data(s);
}
void http2_connection::send_http2_frame(http2_frame_goaway *frame) {
    slice s = pack_http2_frame_goaway(frame);
    send_raw_data(s);
}
void http2_connection::send_http2_frame(http2_frame_window_update *frame) {
    slice s = pack_http2_frame_window_update(frame);
    send_raw_data(s);
}
