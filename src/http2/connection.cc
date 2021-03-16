#include "src/http2/connection.h"
#include "src/http2/settings.h"
#include "src/http2/stream.h"
#include "src/http2/errors.h"
#include "src/http2/frame.h"
#include "src/http2/parser.h"
#include "src/hpack/hpack.h"
#include "src/http2/flow_control.h"
#include "src/utils/log.h"
#include "src/http2/pack.h"
#include "src/http2/flow_control.h"

const uint8_t http2_connection::PREFACE[24] = {0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54,
                                               0x54, 0x50, 0x2f, 0x32, 0x2e, 0x30, 0x0d, 0x0a,
                                               0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a};

http2_connection::http2_connection(http2::SendService *sender, http2::EventHandler *handler,
                                   uint64_t cid, bool client_side)
    : _dynamic_table(g_http2_settings_parameters[HTTP2_SETTINGS_HEADER_TABLE_SIZE].default_value)
    , _sender_service(sender)
    , _event_handler(handler)
    , _connection_id(cid)
    , _client_side(client_side) {

    for (size_t i = 0; i < HTTP2_NUMBER_OF_SETTINGS; i++) {
        _local_settings[i] = g_http2_settings_parameters[i].default_value;
        _remote_settings[i] = g_http2_settings_parameters[i].default_value;
    }

    _flow_control = std::make_shared<ConnectionFlowControl>(_connection_id);
    _flow_control->Initialize();

    _local_settings[HTTP2_SETTINGS_INITIAL_WINDOW_SIZE] = _flow_control->InitialWindowSize();
    _local_settings[HTTP2_SETTINGS_MAX_FRAME_SIZE] = _flow_control->MaxFrameSize();
    _local_settings[HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE] = MAX_HEADER_LIST_SIZE;
    _local_settings[HTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA] =
        GRPC_ALLOW_TRUE_BINARY_METADATA;

    if (client_side) {
        _local_settings[HTTP2_SETTINGS_ENABLE_PUSH] = 0;
        _local_settings[HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS] = 0;
        _next_stream_id = 1;
    } else {
        _next_stream_id = 2;
    }
    hpack::compressor_init(&_send_record);
    hpack::compressor_set_max_table_size(&_send_record,
                                         _remote_settings[HTTP2_SETTINGS_HEADER_TABLE_SIZE]);

    _finish_handshake = false;
    _last_stream_id = 0;

    _next_frame_limit = false;
    _next_stream_id_limit = 0;
    _received_goaway_stream_id = 0;
    _received_goaway = false;
    _sent_goaway_stream_id = 0;
    _sent_goaway = false;

    announced_init_settings();
}

http2_connection::~http2_connection() {
    hpack::compressor_destroy(&_send_record);
}

http2_settings_entry http2_connection::make_settings_entry(http2_setting_id setting_id,
                                                           uint32_t value) {
    http2_settings_entry entry;
    entry.id = g_setting_id_to_wire_id[setting_id];
    entry.value = value;
    return entry;
}

void http2_connection::announced_init_settings() {
    std::vector<http2_settings_entry> vs;
    if (_client_side) {
        // send connection preface
        _sender_service->SendRawData(_connection_id, PREFACE, PREFACE_SIZE);
        _finish_handshake = true;

        vs.emplace_back(make_settings_entry(HTTP2_SETTINGS_ENABLE_PUSH,
                                            local_settings(HTTP2_SETTINGS_ENABLE_PUSH)));
        vs.emplace_back(make_settings_entry(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
                                            local_settings(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)));
    }
    vs.emplace_back(make_settings_entry(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
                                        local_settings(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE)));
    vs.emplace_back(make_settings_entry(HTTP2_SETTINGS_MAX_FRAME_SIZE,
                                        local_settings(HTTP2_SETTINGS_MAX_FRAME_SIZE)));
    vs.emplace_back(make_settings_entry(HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
                                        local_settings(HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE)));

    vs.emplace_back(
        make_settings_entry(HTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA,
                            local_settings(HTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA)));

    http2_frame_settings settings = build_http2_frame_settings(0, vs);
    send_http2_frame(&settings);

    uint32_t window_size_inc = _flow_control->FlushWindowUpdates();
    http2_frame_window_update win_update = build_http2_frame_window_update(0, window_size_inc);
    send_http2_frame(&win_update);
}

uint32_t http2_connection::create_stream() {
    std::lock_guard<std::mutex> lck(_mutex);
    if (_next_stream_id >= http2_stream::MAX_STREAM_ID || _received_goaway) {
        return 0;
    }
    auto stream = std::make_shared<http2_stream>(_flow_control.get(), _next_stream_id);
    _streams[_next_stream_id] = stream;
    _next_stream_id += 2;
    return stream->stream_id();
}

void http2_connection::destroy_stream(uint32_t stream_id) {
    std::lock_guard<std::mutex> lck(_mutex);
    _streams.erase(stream_id);
}

std::shared_ptr<http2_stream> http2_connection::find_stream(uint32_t stream_id) {
    std::lock_guard<std::mutex> lck(_mutex);
    auto it = _streams.find(stream_id);
    if (it == _streams.end()) {
        return nullptr;
    }
    return it->second;
}

uint64_t http2_connection::connection_id() const {
    return _connection_id;
}

bool http2_connection::is_client_side() const {
    return _client_side;
}

bool http2_connection::need_verify_preface() {
    return !_finish_handshake;
}

void http2_connection::verify_preface_done() {
    _finish_handshake = true;
}

void http2_connection::send_goaway(uint32_t error_code, uint32_t last_stream_id,
                                   const std::string &debug) {
    if (last_stream_id == 0) {
        last_stream_id = _last_stream_id;
    }
    http2_frame_goaway frame = build_http2_frame_goaway(error_code, last_stream_id);
    send_http2_frame(&frame);

    _sent_goaway_stream_id = last_stream_id;
    _sent_goaway = true;
}
/*
void http2_connection::async_send_response(std::shared_ptr<http2_response> rsp) {
    // TODO(SHADOW)
    // HEADERS, WINDOW_UPDATE, DATA
    auto stream_id = rsp->StreamId();
    auto stream = find_stream(stream_id);
    if (stream_id > 0 && stream) {
        serialize_http2_response(stream, rsp);
    }
}
*/
void http2_connection::end_of_stream(uint32_t stream_id) {
    auto stream = find_stream(stream_id);
    if (stream) {
        end_of_stream(stream);
        destroy_stream(stream_id);
    }
}
void http2_connection::end_of_stream(std::shared_ptr<http2_stream> &stream) {
    // grpc-status: 0
}

int http2_connection::package_process(const uint8_t *package, uint32_t package_length) {
    if (package_length < HTTP2_FRAME_HEADER_SIZE) {
        log_error("process http2 package, package length(%u) < HTTP2_FRAME_HEADER_SIZE(9)",
                  package_length);
        abort();
        return -1;
    }

    http2_frame_hdr hdr;
    http2_frame_header_unpack(&hdr, package);
    if (package_length < hdr.length + HTTP2_FRAME_HEADER_SIZE) {
        log_error("process http2 package, the payload is incomplete. package length(%u) < need(%u)",
                  package_length, hdr.length + HTTP2_FRAME_HEADER_SIZE);
        abort();
        return -1;
    }

    // check max frame size settings
    uint32_t max_frame_size = local_settings(HTTP2_SETTINGS_MAX_FRAME_SIZE);
    if (hdr.length > max_frame_size) {
        log_error("process http2 package, but length(%u) exceeds the maximum frame size limit(%u)",
                  package_length, max_frame_size);
        send_goaway(HTTP2_FRAME_SIZE_ERROR);
        return -1;
    }

    // check END_HEADERS limit
    if (_next_frame_limit) {
        if (hdr.type != HTTP2_FRAME_CONTINUATION || hdr.stream_id != _next_stream_id_limit) {
            send_goaway(HTTP2_PROTOCOL_ERROR);
            return -1;
        }
    }
    //
    // TODO: when _sent_goaway = true, pass frame
    //

    // record last stream id
    if (hdr.stream_id > _last_stream_id) {
        _last_stream_id = hdr.stream_id;
    }

    std::shared_ptr<http2_stream> stream;
    if (hdr.stream_id > 0) {
        stream = find_stream(hdr.stream_id);
    }

    switch (hdr.type) {
    case HTTP2_FRAME_DATA: {
        http2_frame_data frame;
        parse_http2_frame_data(&hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
        received_data(stream, &frame);
    } break;
    case HTTP2_FRAME_HEADERS: {
        // open stream
        http2_frame_headers frame;
        parse_http2_frame_headers(&hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
        received_header(stream, &frame);
    } break;
    case HTTP2_FRAME_PRIORITY: {
        http2_frame_priority frame;
        parse_http2_frame_priority(&hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
        received_priority(stream, &frame);
    } break;
    case HTTP2_FRAME_RST_STREAM: {
        // close stream
        http2_frame_rst_stream frame;
        parse_http2_frame_rst_stream(&hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
        received_rst_stream(stream, &frame);
    } break;
    case HTTP2_FRAME_SETTINGS: {
        // update settings
        http2_frame_settings frame;
        parse_http2_frame_settings(&hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
        received_settings(&frame);
    } break;
    case HTTP2_FRAME_PUSH_PROMISE: {
        http2_frame_push_promise frame;
        parse_http2_frame_push_promise(&hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
        received_push_promise(stream, &frame);
    } break;
    case HTTP2_FRAME_PING: {
        http2_frame_ping frame;
        parse_http2_frame_ping(&hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
        received_ping(&frame);
    } break;
    case HTTP2_FRAME_GOAWAY: {
        http2_frame_goaway frame;
        parse_http2_frame_goaway(&hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
        received_goaway(&frame);
    } break;
    case HTTP2_FRAME_WINDOW_UPDATE: {
        http2_frame_window_update frame;
        parse_http2_frame_window_update(&hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
        received_window_update(stream, &frame);
    } break;
    case HTTP2_FRAME_CONTINUATION: {
        http2_frame_continuation frame;
        parse_http2_frame_continuation(&hdr, package + HTTP2_FRAME_HEADER_SIZE, &frame);
        received_continuation(stream, &frame);
    } break;
    default:
        log_error("process http2 package, found illegal frame type");
        send_goaway(HTTP2_PROTOCOL_ERROR);
        abort();
        break;
    }

    if (stream) {  // record type & flags
        stream->frame_type(hdr.type);
        stream->frame_flags(hdr.flags);

        if (stream->is_closed()) {
            _streams.erase(stream->stream_id());
        }
    }
    return 0;
}

void http2_connection::received_data(std::shared_ptr<http2_stream> &stream,
                                     http2_frame_data *frame) {
    if (frame->pad_len >= frame->hdr.length) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }
    // Allow empty DATA frames(RFC 7540 )
    if (frame->data.empty()) return;
    if (!stream) {
        _flow_control->RecvData(frame->data.size());
        return;
    }
    stream->append_data(frame->data);
    stream->flow_control()->RecvData(frame->data.size());

    // TODO(SHADOW): do rpc call
    // and if sync response
    std::vector<hpack::mdelem_data> headers;
    headers.push_back({":status", "200"});
    headers.push_back({"content-type", "application/grpc"});
    headers.push_back({"grpc-accept-encoding", "identity,deflate,gzip"});
    headers.push_back({"accept-encoding", "identity,gzip"});
    bool use_true_binary_metadata =
        _remote_settings[HTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA];
    slice_buffer header_block_fragment;
    hpack::compressor_encode_headers(&_send_record, nullptr, &headers, &header_block_fragment,
                                     use_true_binary_metadata);
    // build http2_frame_headers
    // send frame
}

void http2_connection::received_header(std::shared_ptr<http2_stream> &stream,
                                       http2_frame_headers *frame) {
    if (frame->hdr.stream_id == 0) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    if (frame->pad_len >= frame->hdr.length) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }
    if (!stream) {
        stream = std::make_shared<http2_stream>(_flow_control.get(), frame->hdr.stream_id);
        _streams[frame->hdr.stream_id] = stream;
    }

    if (frame->hdr.flags & HTTP2_FLAG_END_HEADERS) {
        _next_frame_limit = false;
        _next_stream_id_limit = 0;
    } else {
        _next_frame_limit = true;
        _next_stream_id_limit = frame->hdr.stream_id;
    }

    slice headers = frame->header_block_fragment;
    std::vector<hpack::mdelem_data> decoded_headers;
    int err =
        hpack::decode_headers(headers.data(), headers.size(), &_dynamic_table, &decoded_headers);
    if (err != HTTP2_NO_ERROR) {
        send_goaway(err);
        return;
    }

    stream->recv_headers(decoded_headers);
}

void http2_connection::received_priority(std::shared_ptr<http2_stream> &stream,
                                         http2_frame_priority *frame) {
    if (frame->hdr.length != 5) {
        send_goaway(HTTP2_FRAME_SIZE_ERROR);
        return;
    }
    if (frame->hdr.stream_id == 0) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    if (stream) {
        stream->set_weight(frame->pspec.weight);
    }
}

void http2_connection::received_rst_stream(std::shared_ptr<http2_stream> &stream,
                                           http2_frame_rst_stream *frame) {
    if (frame->hdr.length != 4) {
        send_goaway(HTTP2_FRAME_SIZE_ERROR);
        return;
    }

    if (stream) {
        stream->recv_rst_stream(frame->error_code);
    }
}

void http2_connection::received_settings(http2_frame_settings *frame) {
    if (frame->hdr.flags & HTTP2_FLAG_ACK) {
        if (frame->hdr.length != 0) {
            send_goaway(HTTP2_FRAME_SIZE_ERROR);
            return;
        }
        return;
    }

    if (frame->hdr.length % 6) {
        send_goaway(HTTP2_FRAME_SIZE_ERROR);
        return;
    }
    if (frame->hdr.stream_id) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    for (size_t i = 0; i < frame->settings.size(); i++) {
        auto settings = frame->settings[i];
        http2_setting_id sid;
        if (!wire_id_to_setting_id(settings.id, &sid)) {
            // skip
            continue;
        }
        _remote_settings[sid] = settings.value;
        if (sid == HTTP2_SETTINGS_HEADER_TABLE_SIZE) {
            hpack::compressor_set_max_table_size(&_send_record, settings.value);
        }
    }

    http2_frame_settings settings_ack = build_http2_frame_settings_ack();
    send_http2_frame(&settings_ack);
}

void http2_connection::received_push_promise(std::shared_ptr<http2_stream> &stream,
                                             http2_frame_push_promise *frame) {
    if (frame->pad_len >= frame->hdr.length) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }
    if (frame->promised_stream_id == 0) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    if (!_local_settings[HTTP2_SETTINGS_ENABLE_PUSH]) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    if (frame->hdr.flags & HTTP2_FLAG_END_HEADERS) {
        _next_frame_limit = false;
        _next_stream_id_limit = 0;
    } else {
        _next_frame_limit = true;
        _next_stream_id_limit = frame->promised_stream_id;
    }

    auto promise_stream = find_stream(frame->promised_stream_id);
    if (!promise_stream) {
        promise_stream =
            std::make_shared<http2_stream>(_flow_control.get(), frame->promised_stream_id);
    } else if (promise_stream->get_state() != http2_stream::IDLE) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    promise_stream->recv_push_promise();

    std::vector<hpack::mdelem_data> decoded_headers;
    slice headers = frame->header_block_fragment;
    int ret =
        hpack::decode_headers(headers.data(), headers.size(), &_dynamic_table, &decoded_headers);
    if (ret != HTTP2_NO_ERROR) {
        send_goaway(ret);
        return;
    }
    if (!decoded_headers.empty()) {
        promise_stream->append_headers(decoded_headers);
    }
}

void http2_connection::received_ping(http2_frame_ping *frame) {
    if (frame->hdr.flags & HTTP2_FLAG_ACK) {
        return;
    }
    if (frame->hdr.length != 8) {
        send_goaway(HTTP2_FRAME_SIZE_ERROR);
        return;
    }
    if (frame->hdr.stream_id) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    http2_frame_ping ping_ack = build_http2_frame_ping(frame->opaque_data, true);
    send_http2_frame(&ping_ack);
}

void http2_connection::received_goaway(http2_frame_goaway *frame) {
    if (frame->hdr.stream_id) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
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
}

void http2_connection::received_window_update(std::shared_ptr<http2_stream> &stream,
                                              http2_frame_window_update *frame) {
    if (frame->hdr.length != 4) {
        send_goaway(HTTP2_FRAME_SIZE_ERROR);
        return;
    }
    if (frame->window_size_inc < 1 || frame->window_size_inc > 2147483647) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    // flow-control
    if (!stream) {
        // for connection
        _flow_control->RecvUpdate(frame->window_size_inc);
    } else {
        // for stream
        stream->flow_control()->RecvUpdate(frame->window_size_inc);
    }
}

void http2_connection::received_continuation(std::shared_ptr<http2_stream> &stream,
                                             http2_frame_continuation *frame) {
    if (!_next_frame_limit) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    if (frame->hdr.flags & HTTP2_FLAG_END_HEADERS) {
        _next_frame_limit = false;
        _next_stream_id_limit = 0;
    }

    slice headers = frame->header_block_fragment;
    std::vector<hpack::mdelem_data> decoded_headers;
    int ret =
        hpack::decode_headers(headers.data(), headers.size(), &_dynamic_table, &decoded_headers);
    if (ret != HTTP2_NO_ERROR) {
        send_goaway(ret);
        return;
    }
    if (stream) {
        stream->append_headers(decoded_headers);
    }
}

void http2_connection::send_tcp_data(slice_buffer &sb) {
    while (!sb.empty()) {
        const slice &s = sb.front();
        _sender_service->SendRawData(_connection_id, s.data(), s.size());
        sb.pop_front();
    }
}

void http2_connection::send_tcp_data(slice s) {
    _sender_service->SendRawData(_connection_id, s.data(), s.size());
}

void http2_connection::send_http2_frame(http2_frame_data *frame) {
    slice_buffer sb = pack_http2_frame_data(frame, local_settings(HTTP2_SETTINGS_MAX_FRAME_SIZE));
    send_tcp_data(sb);
}
void http2_connection::send_http2_frame(http2_frame_headers *frame) {
    slice s = pack_http2_frame_headers(frame);
    send_tcp_data(s);
}
void http2_connection::send_http2_frame(http2_frame_priority *frame) {
    slice s = pack_http2_frame_priority(frame);
    send_tcp_data(s);
}
void http2_connection::send_http2_frame(http2_frame_rst_stream *frame) {
    slice s = pack_http2_frame_rst_stream(frame);
    send_tcp_data(s);
}
void http2_connection::send_http2_frame(http2_frame_settings *frame) {
    slice s = pack_http2_frame_settings(frame);
    send_tcp_data(s);
}
void http2_connection::send_http2_frame(http2_frame_push_promise *frame) {
    slice s = pack_http2_frame_push_promise(frame);
    send_tcp_data(s);
}
void http2_connection::send_http2_frame(http2_frame_ping *frame) {
    slice s = pack_http2_frame_ping(frame);
    send_tcp_data(s);
}
void http2_connection::send_http2_frame(http2_frame_goaway *frame) {
    slice s = pack_http2_frame_goaway(frame);
    send_tcp_data(s);
}
void http2_connection::send_http2_frame(http2_frame_window_update *frame) {
    slice s = pack_http2_frame_window_update(frame);
    send_tcp_data(s);
}
