#include "src/http2/connection.h"
#include "src/http2/stream.h"
#include "src/http2/errors.h"
#include "src/http2/frame.h"
#include "src/http2/parser.h"
#include "src/hpack/hpack.h"
#include "src/utils/log.h"
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
    : _dynamic_table(global_settings_parameters[HTTP2_SETTINGS_HEADER_TABLE_SIZE].default_value)
    , _sender_service(sender)
    , _event_handler(nullptr)
    , _flow_control(nullptr)
    , _connection_id(cid)
    , _client_side(client_side)
    , _ping_pending(false)
    , _settings_pending(false) {

    for (size_t i = 0; i < HTTP2_NUMBER_OF_SETTINGS; i++) {
        _local_settings[i] = global_settings_parameters[i].default_value;
        _remote_settings[i] = global_settings_parameters[i].default_value;
    }

    _local_settings[HTTP2_SETTINGS_INITIAL_WINDOW_SIZE] = INITIAL_WINDOW_SIZE;
    _local_settings[HTTP2_SETTINGS_MAX_FRAME_SIZE] = MAX_FRAME_SIZE;
    _local_settings[HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE] = MAX_HEADER_LIST_SIZE;

    _next_local_stream_id = client_side ? 1 : 2;

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
}

http2_connection::~http2_connection() {
    hpack::compressor_destroy(&_send_record);
}

uint32_t http2_connection::create_stream() {
    if (_next_local_stream_id >= HTTP2_MAX_STREAM_ID || _received_goaway) {
        return 0;
    }
    auto stream = std::make_shared<http2_stream>(_connection_id, _next_local_stream_id);
    _streams[_next_local_stream_id] = stream;
    _next_local_stream_id += 2;
    return stream->stream_id();
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

void http2_connection::set_frame_event_handler(http2::FrameEventHandler *h) {
    _event_handler = h;
}

void http2_connection::set_flow_control_handler(http2::FlowControlHandler *h) {
    _flow_control = h;
}

bool http2_connection::send_ping(uint64_t info) {
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

    http2_frame_settings frame = build_http2_frame_settings(0, vse);
    send_http2_frame(&frame);
    return true;
}

bool http2_connection::send_priority(uint32_t stream_id, uint8_t weight,
                                     uint32_t depend_stream_id) {
    auto stream = find_stream(stream_id);
    if (!stream) return false;
    if (depend_stream_id != 0) {
        auto depend = find_stream(depend_stream_id);
        if (!depend) {
            return false;
        }
    }

    http2_priority_spec spec;
    spec.exclusive = 0;
    spec.weight = weight;
    spec.stream_id = depend_stream_id;

    stream->set_priority_info(&spec);

    http2_frame_priority frame = build_http2_frame_priority(&spec);
    send_http2_frame(&frame);
    return true;
}

bool http2_connection::send_rst_stream(uint32_t stream_id, uint32_t error_code) {
    auto stream = find_stream(stream_id);
    if (!stream) return false;
    http2_frame_rst_stream frame = build_http2_frame_rst_stream(error_code);
    send_http2_frame(&frame);
    stream->send_rst_stream();
    if (stream->is_closed()) {
        _streams.erase(stream_id);
    }
    return true;
}

void http2_connection::send_goaway(uint32_t error_code, uint32_t last_stream_id,
                                   const std::string &debug) {
    if (last_stream_id == 0) {
        last_stream_id = _last_stream_id;
    }
    http2_frame_goaway frame = build_http2_frame_goaway(error_code, last_stream_id, debug);
    send_http2_frame(&frame);

    _sent_goaway_stream_id = last_stream_id;
    _sent_goaway = true;
}

bool http2_connection::send_window_update(uint32_t stream_id, http2::WindowUpdate *wu) {
    wu->connection_window_size_increment &= 0x7fffffff;
    wu->stream_window_size_increment &= 0x7fffffff;

    if (wu->stream_window_size_increment > 0) {
        auto stream = find_stream(stream_id);
        if (!stream) {
            wu->stream_window_size_increment = 0;
        }
    }

    slice s;
    if (wu->stream_window_size_increment > 0) {
        http2_frame_window_update frame =
            build_http2_frame_window_update(stream_id, wu->stream_window_size_increment);
        s += pack_http2_frame_window_update(&frame);
    }
    if (wu->connection_window_size_increment > 0) {
        http2_frame_window_update frame =
            build_http2_frame_window_update(0, wu->connection_window_size_increment);
        s += pack_http2_frame_window_update(&frame);
    }
    send_raw_data(s);
    return !s.empty();
}

bool http2_connection::send_request(http2::Request *request) {
    auto stream = find_stream(request->stream_id_);
    if (!stream) return false;

    if (request->data_.empty()) {
        int flags = HTTP2_FLAG_END_HEADERS;
        if (request->finish_) {
            flags |= HTTP2_FLAG_END_STREAM;
        }
        send_binary_in_headers_frame(stream, request->headers_, flags);
        stream->send_headers();
        if (request->finish_) {
            stream->send_end_stream();
        }
        return true;
    }
    stream->send_headers();
    send_binary_in_headers_frame(stream, request->headers_, HTTP2_FLAG_END_HEADERS);
    send_binary_in_data_frame(stream, request->data_, request->finish_);
    if (request->finish_) {
        stream->send_end_stream();
    }
    if (stream->is_closed()) {
        destroy_stream(stream->stream_id());
    }
    return true;
}

bool http2_connection::send_response(http2::Response *response) {
    auto stream = find_stream(response->stream_id_);
    if (!stream) return false;

    if (!response->initlize_headers_.empty()) {
        send_binary_in_headers_frame(stream, response->initlize_headers_, HTTP2_FLAG_END_HEADERS);
        response->initlize_headers_.clear();
        stream->send_headers();
    }
    if (response->data_.size() > 0) {
        bool end_of_stream = (response->finish_ && response->trailing_headers_.empty());
        send_binary_in_data_frame(stream, response->data_, end_of_stream);
        response->data_.clear();
    }
    if (!response->trailing_headers_.empty()) {
        int flags = HTTP2_FLAG_END_HEADERS;
        if (response->finish_) {
            flags |= HTTP2_FLAG_END_STREAM;
        }
        send_binary_in_headers_frame(stream, response->trailing_headers_, flags);
        stream->send_headers();
    }
    if (response->finish_) {
        stream->send_end_stream();
    }
    if (stream->is_closed()) {
        destroy_stream(stream->stream_id());
    }
    return true;
}

uint32_t
http2_connection::send_push_promise(std::vector<std::pair<std::string, std::string>> *headers) {
    uint32_t stream_id = create_stream();
    if (stream_id == 0) {
        return 0;
    }

    std::vector<hpack::mdelem_data> mdels;
    for (size_t i = 0; i < headers->size(); i++) {
        mdels.push_back({headers->at(i).first, headers->at(i).second});
    }

    bool use_true_binary_metadata = true;
    slice_buffer header_block_fragment;
    hpack::compressor_encode_headers(&_send_record, &mdels, &header_block_fragment,
                                     use_true_binary_metadata);

    http2_frame_push_promise frame =
        build_http2_frame_push_promise(stream_id, &header_block_fragment);
    send_http2_frame(&frame);
    return stream_id;
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

    http2_frame_headers frame =
        build_http2_frame_headers(stream->stream_id(), &header_block_fragment, flags);
    send_http2_frame(&frame);
}

void http2_connection::send_binary_in_data_frame(std::shared_ptr<http2_stream> &stream,
                                                 const std::vector<std::string> &binary,
                                                 bool end_of_stream) {
    if (binary.empty()) return;

    size_t block_count = binary.size() - 1;
    for (size_t i = 0; i < block_count; i++) {
        slice data(binary[i].data(), binary[i].size());
        if (_flow_control) {
            http2::WindowUpdate wu =
                _flow_control->OnPreSendData(_connection_id, stream->stream_id(), data.size());
            send_window_update(stream->stream_id(), &wu);
        }
        http2_frame_data frame = build_http2_frame_data(stream->stream_id(), data, 0);
        send_http2_frame(&frame);
    }

    slice data(binary[block_count].data(), binary[block_count].size());
    if (_flow_control) {
        http2::WindowUpdate wu =
            _flow_control->OnPreSendData(_connection_id, stream->stream_id(), data.size());
        send_window_update(stream->stream_id(), &wu);
    }
    int flags = end_of_stream ? HTTP2_FLAG_END_STREAM : 0;
    http2_frame_data frame = build_http2_frame_data(stream->stream_id(), data, flags);
    send_http2_frame(&frame);
}

int http2_connection::package_process(const uint8_t *package, uint32_t package_length) {
    LOG_ASSERT(package_length >= HTTP2_FRAME_HEADER_SIZE);

    http2_frame_hdr hdr;
    http2_frame_header_unpack(&hdr, package);

    LOG_ASSERT(package_length >= hdr.length + HTTP2_FRAME_HEADER_SIZE);

    // check END_HEADERS limit
    if (_next_frame_limit) {
        if (hdr.type != HTTP2_FRAME_CONTINUATION || hdr.stream_id != _next_stream_id_limit) {
            log_error("http2_connection: current frame MUST BE CONTINUATION");
            send_goaway(HTTP2_PROTOCOL_ERROR);
            return -1;
        }
    }
    //
    // TODO: when _sent_goaway = true
    //

    // record last stream id
    if (hdr.stream_id > _last_stream_id) {
        _last_stream_id = hdr.stream_id;
    }

    if (hdr.type > HTTP2_FRAME_CONTINUATION) {
        log_warn("http2_connection: found unknown frame type (%d)", static_cast<int>(hdr.type));
        return static_cast<int>(hdr.length + HTTP2_FRAME_HEADER_SIZE);
    }

    std::shared_ptr<http2_stream> stream = frame_process_func_array[hdr.type](this, &hdr, package);

    if (stream && stream->is_closed()) {
        destroy_stream(stream->stream_id());
    }
    return static_cast<int>(hdr.length + HTTP2_FRAME_HEADER_SIZE);
}

void http2_connection::received_data(std::shared_ptr<http2_stream> &stream,
                                     http2_frame_data *frame) {
    if (frame->pad_len >= frame->hdr.length) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    if (frame->data.empty()) return;
    if (!stream) return;

    stream->append_data(frame->data);
    stream->save_frame_info(&frame->hdr);

    if (_flow_control) {
        _flow_control->OnDataReceived(_connection_id, stream->stream_id(), frame->data.size());
    }
    _event_handler->OnData(stream->get_shared_stream());
}

void http2_connection::received_headers(std::shared_ptr<http2_stream> &stream,
                                        http2_frame_headers *frame) {
    if (frame->hdr.stream_id == 0) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    if (frame->pad_len >= frame->hdr.length) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    if (_client_side) {
        if (frame->hdr.stream_id & 1) {
            send_goaway(HTTP2_PROTOCOL_ERROR);
            return;
        }
    } else {
        if (!(frame->hdr.stream_id & 1)) {
            send_goaway(HTTP2_PROTOCOL_ERROR);
            return;
        }
    }

    slice headers = frame->header_block_fragment;
    std::vector<hpack::mdelem_data> decoded_headers;
    int err =
        hpack::decode_headers(headers.data(), headers.size(), &_dynamic_table, &decoded_headers);
    if (err != HTTP2_NO_ERROR) {
        send_goaway(err);
        return;
    }

    if (!stream) {
        stream = std::make_shared<http2_stream>(_connection_id, frame->hdr.stream_id);
        _streams[frame->hdr.stream_id] = stream;
    }

    stream->save_frame_info(&frame->hdr);
    stream->recv_headers(decoded_headers);

    if (frame->hdr.flags & HTTP2_FLAG_END_HEADERS) {
        _next_frame_limit = false;
        _next_stream_id_limit = 0;
        _event_handler->OnHeaders(stream->get_shared_stream(), false);
    } else {
        _next_frame_limit = true;
        _next_stream_id_limit = frame->hdr.stream_id;
    }
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

    // A stream cannot depend on itself
    if (frame->hdr.stream_id == frame->pspec.stream_id) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    if (stream) {
        stream->set_priority_info(&frame->pspec);
        stream->save_frame_info(&frame->hdr);
        _event_handler->OnPriority(stream->get_shared_stream());
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
        stream->save_frame_info(&frame->hdr);
        _event_handler->OnRSTStream(stream->get_shared_stream());
    }
}

void http2_connection::received_settings(http2_frame_settings *frame) {
    if (frame->hdr.length % 6) {
        send_goaway(HTTP2_FRAME_SIZE_ERROR);
        return;
    }

    if (frame->hdr.stream_id) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    if (frame->hdr.flags & HTTP2_FLAG_ACK) {
        if (frame->hdr.length != 0) {
            send_goaway(HTTP2_FRAME_SIZE_ERROR);
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

        _event_handler->OnSettings(_connection_id, 0, 0, true);
        return;
    }
    for (size_t i = 0; i < frame->settings.size(); i++) {
        http2_settings_entry setting = frame->settings[i];
        if (setting.id < HTTP2_NUMBER_OF_SETTINGS) {
            _remote_settings[setting.id] = setting.value;
            if (setting.id == HTTP2_SETTINGS_HEADER_TABLE_SIZE) {
                hpack::compressor_set_max_table_size(&_send_record, setting.value);
            }
        }
        _event_handler->OnSettings(_connection_id, setting.id, setting.value, false);
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

    auto promise_stream = find_stream(frame->promised_stream_id);
    if (!promise_stream) {
        promise_stream = std::make_shared<http2_stream>(_connection_id, frame->promised_stream_id);
    } else if (promise_stream->get_state() != HTTP2_STREAM_IDLE) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    std::vector<hpack::mdelem_data> decoded_headers;
    slice headers = frame->header_block_fragment;
    int ret =
        hpack::decode_headers(headers.data(), headers.size(), &_dynamic_table, &decoded_headers);
    if (ret != HTTP2_NO_ERROR) {
        send_goaway(ret);
        return;
    }

    promise_stream->recv_push_promise();
    promise_stream->save_frame_info(&frame->hdr);
    promise_stream->append_headers(decoded_headers);

    if (frame->hdr.flags & HTTP2_FLAG_END_HEADERS) {
        _next_frame_limit = false;
        _next_stream_id_limit = 0;
        _event_handler->OnHeaders(promise_stream->get_shared_stream(), true);
    } else {
        _next_frame_limit = true;
        _next_stream_id_limit = frame->promised_stream_id;
    }
}

void http2_connection::received_ping(http2_frame_ping *frame) {
    if (frame->hdr.length != 8) {
        send_goaway(HTTP2_FRAME_SIZE_ERROR);
        return;
    }
    if (frame->hdr.stream_id) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    uint64_t ping_data;
    memcpy(&ping_data, frame->opaque_data, 8);
    if (frame->hdr.flags & HTTP2_FLAG_ACK) {
        _ping_pending = false;
        _event_handler->OnPing(_connection_id, ping_data, true);
    } else {
        _event_handler->OnPing(_connection_id, ping_data, false);
        http2_frame_ping ping_ack = build_http2_frame_ping(frame->opaque_data, true);
        send_http2_frame(&ping_ack);
    }
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
    _event_handler->OnGoAway(_connection_id, frame->last_stream_id, frame->error_code,
                             frame->debug_data.to_string());
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
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    if (!stream) {
        send_goaway(HTTP2_PROTOCOL_ERROR);
        return;
    }

    slice headers = frame->header_block_fragment;
    std::vector<hpack::mdelem_data> decoded_headers;
    int ret =
        hpack::decode_headers(headers.data(), headers.size(), &_dynamic_table, &decoded_headers);
    if (ret != HTTP2_NO_ERROR) {
        send_goaway(ret);
        return;
    }

    stream->append_headers(decoded_headers);
    stream->save_frame_info(&frame->hdr);

    if (frame->hdr.flags & HTTP2_FLAG_END_HEADERS) {
        _next_frame_limit = false;
        _next_stream_id_limit = 0;
        _event_handler->OnHeaders(stream->get_shared_stream(), false);
    }
}

void http2_connection::send_raw_data(const slice_buffer &sb) {
    for (size_t i = 0; i < sb.slice_count(); i++) {
        const slice &s = sb[i];
        _sender_service->SendRawData(_connection_id, s.data(), s.size());
    }
}

void http2_connection::send_raw_data(slice s) {
    _sender_service->SendRawData(_connection_id, s.data(), s.size());
}

void http2_connection::send_http2_frame(http2_frame_data *frame) {
    slice_buffer sb = pack_http2_frame_data(frame, local_settings(HTTP2_SETTINGS_MAX_FRAME_SIZE));
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
