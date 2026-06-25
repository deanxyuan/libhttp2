/**
 * @file http2.cc
 * @brief HTTP/2 transport implementation -- provides the TransportAdaptor class
 *        that bridges the public Transport interface to the internal http2_connection.
 */

#include "http2/transport.h"
#include <mutex>
#include <string.h>
#include "src/http2/connection.h"
#include "src/http2/stream.h"
#include "src/http2/frame.h"
#include "src/hpack/static_metadata.h"

namespace {
std::mutex global_init_mutex;
int32_t global_init_counter = 0;
}  // namespace

/** @brief Initialize the library-wide static metadata context (once, thread-safe). */
static void internal_init_library() {
    std::lock_guard<std::mutex> lck(global_init_mutex);
    if (global_init_counter == 0) {
        init_static_metadata_context();
    }
    global_init_counter++;
}

/** @brief Release the library-wide static metadata context when last reference is dropped. */
static void internal_cleanup_library() {
    std::lock_guard<std::mutex> lck(global_init_mutex);
    if (global_init_counter <= 0) return;
    if (global_init_counter == 1) {
        destroy_static_metadata_context();
    }
    global_init_counter--;
}

/** @brief Tag type used to restrict LibraryInitializer construction. */
struct InternalTag {};

/** @brief RAII helper that initializes/cleans up the library-wide static metadata context. */
class LibraryInitializer {
public:
    explicit LibraryInitializer(InternalTag) {
        internal_init_library();
    }
    ~LibraryInitializer() {
        internal_cleanup_library();
    }
};

namespace http2 {

/** @brief Concrete Transport implementation that delegates to http2_connection. */
class TransportAdaptor : public Transport {
public:
    /**
     * @brief Construct a TransportAdaptor wrapping a new http2_connection.
     * @param service   SendService used for raw data output.
     * @param cid       Unique connection identifier.
     * @param client_side  True if this endpoint is the HTTP/2 client.
     */
    TransportAdaptor(SendService *service, uint64_t cid, bool client_side);

    /** @brief Destructor. */
    ~TransportAdaptor();

    // === Transport interface ===

    void SetEventHandler(EventHandler *handler) override;
    void SetFlowControlHandler(FlowControlHandler *handler) override;
    uint64_t GetConnectionId() const override;
    bool IsClientSide() const override;
    std::shared_ptr<Stream> CreateStream() override;
    bool SendSettings(const std::vector<std::pair<uint16_t, uint32_t>> &settings) override;
    bool SendPing(uint64_t info) override;
    bool SendGoAway(uint32_t error_code, uint32_t last_stream_id,
                    const std::string &debug) override;
    std::shared_ptr<Stream> SendPushPromise(
        std::shared_ptr<Stream> request_stream,
        const std::vector<std::pair<std::string, std::string>> &headers) override;
    int ReceivedData(const uint8_t *buf, uint32_t len) override;
    void Drain() override;
    void Shutdown() override;

    std::shared_ptr<Stream> FindStream(uint32_t stream_id) override;
    uint32_t GetRemoteSetting(uint16_t id) const override;
    uint32_t GetLocalSetting(uint16_t id) const override;
    http2::ConnectionInfo GetConnectionInfo() const override;
    bool SendWindowUpdate(uint32_t stream_id, uint32_t increment) override;
    void SetBufferedMode(bool enable) override;
    bool Flush() override;

private:
    LibraryInitializer _internal;
    http2_connection _impl;
};

// ============================================================================
// Construction / destruction
// ============================================================================

TransportAdaptor::TransportAdaptor(SendService *service, uint64_t cid, bool client_side)
    : _internal(InternalTag())
    , _impl(service, cid, client_side) {}

TransportAdaptor::~TransportAdaptor() {}

// ============================================================================
// Handler setters
// ============================================================================

void TransportAdaptor::SetEventHandler(EventHandler *handler) {
    _impl.set_event_handler(handler);
}

void TransportAdaptor::SetFlowControlHandler(FlowControlHandler *handler) {
    _impl.set_flow_control_handler(handler);
}

// ============================================================================
// Connection queries
// ============================================================================

uint64_t TransportAdaptor::GetConnectionId() const {
    return _impl.connection_id();
}

bool TransportAdaptor::IsClientSide() const {
    return _impl.is_client_side();
}

// ============================================================================
// Stream creation
// ============================================================================

std::shared_ptr<Stream> TransportAdaptor::CreateStream() {
    return std::static_pointer_cast<Stream>(_impl.create_stream());
}

// ============================================================================
// Transport-level send operations
// ============================================================================

bool TransportAdaptor::SendSettings(const std::vector<std::pair<uint16_t, uint32_t>> &settings) {
    return _impl.send_settings(settings);
}

bool TransportAdaptor::SendPing(uint64_t info) {
    return _impl.send_ping(info);
}

bool TransportAdaptor::SendGoAway(uint32_t error_code, uint32_t last_stream_id,
                                  const std::string &debug) {
    return _impl.send_goaway(error_code, last_stream_id, debug);
}

std::shared_ptr<Stream> TransportAdaptor::SendPushPromise(
    std::shared_ptr<Stream> request_stream,
    const std::vector<std::pair<std::string, std::string>> &headers) {
    auto req = std::dynamic_pointer_cast<http2_stream>(request_stream);
    if (!req) return nullptr;
    return std::static_pointer_cast<Stream>(_impl.send_push_promise(req.get(), headers));
}

// ============================================================================
// ReceivedData -- feed incoming TCP data to the transport
// ============================================================================

int TransportAdaptor::ReceivedData(const uint8_t *buf, uint32_t len) {
    const uint8_t *start = buf;

    // --- Client preface verification (server side only) ---
    if (_impl.need_verify_preface()) {
        if (len < static_cast<uint32_t>(http2_connection::PREFACE_SIZE)) {
            return 0;  // need more data
        }
        if (memcmp(buf, http2_connection::PREFACE, http2_connection::PREFACE_SIZE) != 0) {
            _impl.send_goaway(static_cast<uint32_t>(Http2ErrorCode::ProtocolError));
            return -1;
        }
        _impl.verify_preface_done();
        _impl.send_initial_settings();
        buf += http2_connection::PREFACE_SIZE;
        len -= http2_connection::PREFACE_SIZE;
    }

    // --- Frame loop: parse header, validate size, process ---
    while (len >= HTTP2_FRAME_HEADER_SIZE) {
        // Unpack the 9-byte frame header to get the payload length.
        http2_frame_hdr hdr;
        http2_frame_header_unpack(&hdr, buf);

        // Check frame size against our local MAX_FRAME_SIZE setting.
        if (hdr.length > _impl.local_max_frame_size()) {
            _impl.send_goaway(static_cast<uint32_t>(Http2ErrorCode::FrameSizeError));
            return -1;
        }

        uint32_t frame_total = hdr.length + HTTP2_FRAME_HEADER_SIZE;
        if (frame_total > len) {
            break;  // incomplete frame -- need more data
        }

        // Process the complete frame.
        int consumed = _impl.package_process(buf, frame_total);
        if (consumed < 0) {
            return -1;
        }
        buf += consumed;
        len -= consumed;
    }

    return static_cast<int>(buf - start);
}

// ============================================================================
// Drain -- graceful shutdown
// ============================================================================

void TransportAdaptor::Drain() {
    _impl.drain();
}

// ============================================================================
// Shutdown
// ============================================================================

void TransportAdaptor::Shutdown() {
    _impl.send_goaway(static_cast<uint32_t>(Http2ErrorCode::NoError));
}

// ============================================================================
// New query methods
// ============================================================================

std::shared_ptr<Stream> TransportAdaptor::FindStream(uint32_t stream_id) {
    return std::static_pointer_cast<Stream>(_impl.find_stream(stream_id));
}

uint32_t TransportAdaptor::GetRemoteSetting(uint16_t id) const {
    return _impl.remote_setting(id);
}

uint32_t TransportAdaptor::GetLocalSetting(uint16_t id) const {
    return _impl.local_setting(id);
}

http2::ConnectionInfo TransportAdaptor::GetConnectionInfo() const {
    return _impl.get_connection_info();
}

void TransportAdaptor::SetBufferedMode(bool enable) {
    _impl.set_buffered_mode(enable);
}

bool TransportAdaptor::Flush() {
    return _impl.flush_buffer();
}

// ============================================================================
// SendWindowUpdate
// ============================================================================

bool TransportAdaptor::SendWindowUpdate(uint32_t stream_id, uint32_t increment) {
    http2::WindowUpdate wu;
    if (stream_id == 0) {
        wu.connection_window_size_increment = increment;
        wu.stream_window_size_increment = 0;
    } else {
        wu.connection_window_size_increment = 0;
        wu.stream_window_size_increment = increment;
    }
    return _impl.send_window_update(stream_id, wu);
}

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<Transport> CreateTransport(uint64_t connection_id, bool client_side,
                                           SendService *service) {
    return std::unique_ptr<Transport>(new TransportAdaptor(service, connection_id, client_side));
}

}  // namespace http2
