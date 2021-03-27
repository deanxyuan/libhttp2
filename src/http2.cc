#include "http2/transport.h"
#include "src/http2/connection.h"
#include "src/http2/errors.h"
#include "src/hpack/static_metadata.h"

#include "src/utils/log.h"

namespace {
std::mutex global_init_mutex;
int32_t global_init_counter = 0;
}  // namespace

static void internal_init_library() {
    std::lock_guard<std::mutex> lck(global_init_mutex);
    if (global_init_counter == 0) {
        init_static_metadata_context();
    }
    global_init_counter++;
}

static void internal_cleanup_library() {
    std::lock_guard<std::mutex> lck(global_init_mutex);
    if (global_init_counter == 1) {
        destroy_static_metadata_context();
    }
    global_init_counter--;
}
struct InternalTag {};

class LibraryInitlizer {
public:
    explicit LibraryInitlizer(InternalTag) {
        internal_init_library();
    }
    ~LibraryInitlizer() {
        internal_cleanup_library();
    }

    int temp() {
        return 42;
    }
};

namespace http2 {
class TransportAdaptor : public Transport {
public:
    TransportAdaptor(SendService *service, uint64_t cid, bool client_side);
    ~TransportAdaptor();

    uint64_t GetConnectionId() override;
    bool IsClientSide() override;

    void SetFlowControlHandler(FlowControlHandler *handler) override;
    void SetFrameEventHandler(FrameEventHandler *handler) override;

    bool SendPing(uint64_t info) override;
    bool SendSettings(const std::vector<std::pair<uint16_t, uint32_t>> &settings) override;
    bool SendPriority(uint32_t stream_id, uint8_t weight, uint32_t depend_stream_id) override;

    bool SendRSTStream(uint32_t stream_id, uint32_t error_code) override;
    void SendGoAway(uint32_t last_stream_id, uint32_t error_code, const std::string &debug) override;
    bool SendWindowUpdate(uint32_t stream_id, WindowUpdate *wu) override;

    bool SendRequest(Request *req) override;
    bool SendResponse(Response *rsp) override;

    // if initlize_headers not null, send PUSH_PROMISE
    // else just allocate local stream id
    uint32_t CreateStream(std::vector<std::pair<std::string, std::string>> *initlize_headers) override;

    /*
     * Return -1 means an error occurred, return 0 means still need to provide
     * data, other values greater than 0 means the length of a complete http2
     * packet. When calling the ReceivedData function, at least one complete
     * http2 package must be provided.
     */
    int CheckPackageLength(const uint8_t *data, uint32_t len) override;

    /*
     * Need to provide one or more complete http2 data package. To get the
     * complete http2 package, please check CheckHttp2PackageLength function.
     */
    void ReceivedData(const uint8_t *buf, uint32_t len) override;

    // Call this function to notify a connection to be disconnected.
    void Shutdown() override;

private:
    LibraryInitlizer _internal;
    http2_connection _impl;
};

TransportAdaptor::TransportAdaptor(SendService *service, uint64_t cid, bool client_side)
    : _internal(InternalTag())
    , _impl(service, cid, client_side) {}

TransportAdaptor::~TransportAdaptor() {}

uint64_t TransportAdaptor::GetConnectionId() {
    return _impl.connection_id();
}

bool TransportAdaptor::IsClientSide() {
    return _impl.is_client_side();
}

void TransportAdaptor::SetFlowControlHandler(FlowControlHandler *handler) {
    _impl.set_flow_control_handler(handler);
}

void TransportAdaptor::SetFrameEventHandler(FrameEventHandler *handler) {
    _impl.set_frame_event_handler(handler);
}

bool TransportAdaptor::SendPing(uint64_t info) {
    return _impl.send_ping(info);
}

bool TransportAdaptor::SendSettings(const std::vector<std::pair<uint16_t, uint32_t>> &settings) {
    return _impl.send_settings(settings);
}

bool TransportAdaptor::SendPriority(uint32_t stream_id, uint8_t weight, uint32_t depend_stream_id) {
    return _impl.send_priority(stream_id, weight, depend_stream_id);
}

bool TransportAdaptor::SendRSTStream(uint32_t stream_id, uint32_t error_code) {
    return _impl.send_rst_stream(stream_id, error_code);
}

void TransportAdaptor::SendGoAway(uint32_t last_stream_id, uint32_t error_code, const std::string &debug) {
    _impl.send_goaway(error_code, last_stream_id, debug);
}

bool TransportAdaptor::SendWindowUpdate(uint32_t stream_id, WindowUpdate *wu) {
    if (!wu) return false;
    return _impl.send_window_update(stream_id, wu);
}

bool TransportAdaptor::SendRequest(Request *req) {
    if (!req) return false;
    return _impl.send_request(req);
}

bool TransportAdaptor::SendResponse(Response *rsp) {
    if (!rsp) return false;
    return _impl.send_response(rsp);
}

uint32_t TransportAdaptor::CreateStream(std::vector<std::pair<std::string, std::string>> *headers) {
    if (headers && !headers->empty()) {
        return _impl.send_push_promise(headers);
    }
    return _impl.create_stream();
}

/*
 * Return -1 means an error occurred, return 0 means still need to provide
 * data, other values greater than 0 means the length of a complete http2
 * packet. When calling the ReceivedData function, at least one complete
 * http2 package must be provided.
 */
int TransportAdaptor::CheckPackageLength(const uint8_t *data, uint32_t data_size) {
    if (data_size < HTTP2_FRAME_HEADER_SIZE) return 0;

    int64_t total_pack_len = 0;

    if (_impl.need_verify_preface() && memcmp(data, http2_connection::PREFACE, 4) == 0) {
        if (data_size < http2_connection::PREFACE_SIZE) {
            return http2_connection::PREFACE_SIZE;
        }
        total_pack_len += http2_connection::PREFACE_SIZE;
        data_size -= http2_connection::PREFACE_SIZE;
        data += http2_connection::PREFACE_SIZE;
    }

    while (data_size >= HTTP2_FRAME_HEADER_SIZE) {
        http2_frame_hdr hdr;
        http2_frame_header_unpack(&hdr, data);
        if (hdr.length > _impl.local_max_frame_size()) {
            _impl.send_goaway(HTTP2_FRAME_SIZE_ERROR);
            log_warn("TransportAdaptor: hdr.length(%lu) > max_frame_size(%lu)", hdr.length,
                     _impl.local_max_frame_size());
            return -1;
        }

        uint32_t pack_len = hdr.length + HTTP2_FRAME_HEADER_SIZE;
        if (total_pack_len + pack_len > INT32_MAX) {
            break;
        }

        total_pack_len += pack_len;

        if (pack_len > data_size) {
            break;
        }

        data_size -= pack_len;
        data += pack_len;
    }

    return static_cast<int>(total_pack_len);
}

/*
 * Need to provide one or more complete http2 data package. To get the
 * complete http2 package, please check CheckHttp2PackageLength function.
 */
void TransportAdaptor::ReceivedData(const uint8_t *package, uint32_t package_length) {
    if (_impl.need_verify_preface()) {
        if (package_length < http2_connection::PREFACE_SIZE ||
            memcmp(package, http2_connection::PREFACE, http2_connection::PREFACE_SIZE) != 0) {
            _impl.send_goaway(HTTP2_PROTOCOL_ERROR);
            return;
        }
        _impl.verify_preface_done();
        package_length -= http2_connection::PREFACE_SIZE;
        package += http2_connection::PREFACE_SIZE;
    }

    while (package_length > 0) {
        int result = _impl.package_process(package, package_length);
        if (result == -1) {
            break;
        }
        package_length -= result;
        package += result;
    }
}

// Call this function to notify a connection to be disconnected.
void TransportAdaptor::Shutdown() {
    // TODO(SHADOW): cleanup
}

Transport *CreateTransport(uint64_t connection_id, bool client_side, SendService *service) {
    auto transport = new TransportAdaptor(service, connection_id, client_side);
    return transport;
}

void DestroyTransport(Transport *transport) {
    delete transport;
}

void SetLogPrintFunction(void (*print_func)(const char *file, int line, int level, const char *message)) {
    LogSetPrintFunction(print_func);
}
void SetLogLevel(int level) {
    LogSetLevel(level);
}

Request::Request()
    : stream_id_(0)
    , finish_(false) {}

Request::~Request() {}

void Request::SetStreamId(uint32_t sid) {
    stream_id_ = sid;
}

void Request::AddMetadata(const std::string &key, const std::string &value) {
    headers_.push_back({key, value});
}

void Request::AppendData(const uint8_t *data, uint32_t size) {
    data_.push_back(std::string(reinterpret_cast<const char *>(data), size));
}

void Request::Finish() {
    finish_ = true;
}

void Request::Reset() {
    stream_id_ = 0;
    headers_.clear();
    data_.clear();
    finish_ = false;
}

Response::Response()
    : stream_id_(0)
    , finish_(false) {}

Response::~Response() {}

void Response::SetStreamId(uint32_t sid) {
    stream_id_ = sid;
}

void Response::AddInitlizeMetadata(const std::string &key, const std::string &value) {
    initlize_headers_.push_back({key, value});
}

void Response::AddTrailingMetadata(const std::string &key, const std::string &value) {
    trailing_headers_.push_back({key, value});
}

void Response::AppendData(const uint8_t *data, uint32_t size) {
    data_.push_back(std::string(reinterpret_cast<const char *>(data), size));
}

void Response::Finish() {
    finish_ = true;
}

void Response::Reset() {
    stream_id_ = 0;
    initlize_headers_.clear();
    trailing_headers_.clear();
    data_.clear();
    finish_ = false;
}

}  // namespace http2
