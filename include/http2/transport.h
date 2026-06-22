/** @file transport.h
 *  @brief Core HTTP/2 transport interface and supporting types (RFC 7540).
 *
 *  Defines the public API for the LibHttp2 transport layer. The design is
 *  Stream-centric: streams own send operations, eliminating the separate
 *  Request/Response builder classes. Flow control is automatic by default.
 *
 *  Concepts:
 *    - Transport  — one per TCP connection, manages protocol state
 *    - Stream     — one per HTTP/2 stream, can send and receive
 *    - EventHandler — callback interface for incoming events
 *    - SendService  — output bridge to your TCP layer
 */
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// HTTP/2 Enum Classes (RFC 7540)
// ============================================================================

/** @brief Error codes used in RST_STREAM and GOAWAY frames (RFC 7540, Section 7). */
enum class Http2ErrorCode : uint32_t {
    NoError = 0x0,
    ProtocolError = 0x1,
    InternalError = 0x2,
    FlowControlError = 0x3,
    SettingsTimeout = 0x4,
    StreamClosed = 0x5,
    FrameSizeError = 0x6,
    RefusedStream = 0x7,
    Cancel = 0x8,
    CompressionError = 0x9,
    ConnectError = 0xa,
    EnhanceYourCalm = 0xb,
    InadequateSecurity = 0xc,
    Http11Required = 0xd,
};

/** @brief HTTP/2 frame types (RFC 7540, Section 6). */
enum class Http2FrameType : uint8_t {
    Data = 0x0,
    Headers = 0x1,
    Priority = 0x2,
    RstStream = 0x3,
    Settings = 0x4,
    PushPromise = 0x5,
    Ping = 0x6,
    GoAway = 0x7,
    WindowUpdate = 0x8,
    Continuation = 0x9,
};

/** @brief HTTP/2 SETTINGS parameter identifiers (RFC 7540, Section 6.5.2). */
enum class Http2SettingsId : uint16_t {
    None = 0,
    HeaderTableSize = 1,
    EnablePush = 2,
    MaxConcurrentStreams = 3,
    InitialWindowSize = 4,
    MaxFrameSize = 5,
    MaxHeaderListSize = 6,
    COUNT = 7,
};

/** @brief Behavior when a SETTINGS value is outside the valid range. */
enum class InvalidValueBehavior {
    Clamp,       /**< Clamp the value to the nearest valid boundary. */
    Disconnect,  /**< Treat it as a connection error and disconnect. */
};

/** @brief Describes the valid range and behavior for a single SETTINGS parameter. */
struct SettingParameters {
    const char *name;                            /**< Human-readable setting name. */
    uint32_t default_value;                      /**< RFC 7540 default value. */
    uint32_t min_value;                          /**< Minimum allowed value. */
    uint32_t max_value;                          /**< Maximum allowed value. */
    InvalidValueBehavior invalid_value_behavior;  /**< What to do if a peer sends an out-of-range value. */
    Http2ErrorCode error_value;                  /**< Error code used when clamping or disconnecting. */
};

/** @brief Total number of entries in the settings array (COUNT = 7). */
constexpr size_t HTTP2_NUMBER_OF_SETTINGS = static_cast<size_t>(Http2SettingsId::COUNT);

/** @brief HTTP/2 stream states (RFC 7540, Section 5.1). */
enum class Http2StreamState : int {
    Idle = 0,
    ReservedLocal = 1,
    ReservedRemote = 2,
    Open = 3,
    HalfClosedLocal = 4,
    HalfClosedRemote = 5,
    Closed = 6,
};

/** @brief HTTP/2 frame flags (RFC 7540, Section 4.1 and individual frame sections). */
enum class Http2FrameFlag : uint8_t {
    None = 0x0,
    EndStream = 0x1,
    Ack = 0x1,         /**< Same value as EndStream, different context (SETTINGS, PING). */
    EndHeaders = 0x4,
    Padded = 0x8,
    Priority = 0x20,
};

/** @brief Maximum valid stream identifier (2^31 - 1). */
constexpr uint32_t HTTP2_MAX_STREAM_ID = 0x7FFFFFFF;

namespace http2 {

/** @brief Global table of valid ranges for all HTTP/2 SETTINGS parameters. */
const extern SettingParameters global_settings_parameters[HTTP2_NUMBER_OF_SETTINGS];

/** @brief ALPN protocol identifier for HTTP/2 over TLS (RFC 7540, Section 3.3). */
constexpr const char* ALPN_PROTOCOL = "h2";

/** @brief Length of the ALPN protocol identifier (2 bytes). */
constexpr size_t ALPN_PROTOCOL_LEN = 2;

// ============================================================================
// SendService — output bridge to TCP
// ============================================================================

/** @brief Output interface for sending raw TCP data.
 *
 *  Implement this interface to bridge libhttp2 to your network I/O layer.
 *  The library calls SendRawData() whenever HTTP/2 frames need to be
 *  transmitted over the wire.
 */
class SendService {
public:
    virtual ~SendService() {}

    /** @brief Send raw bytes over the TCP connection.
     *
     *  Called by the library when serialized HTTP/2 frames are ready to send.
     *  The implementation must write the bytes to the TCP connection identified
     *  by the connection id.
     *
     *  @param cid  Connection identifier, managed by the external network module.
     *  @param buf  Pointer to the data to send.
     *  @param size Length of the data in bytes.
     *  @return >0: bytes sent immediately (synchronous completion)
     *           0: data queued for async delivery (call OnSendComplete later)
     *          <0: error (connection should be closed)
     */
    virtual int SendRawData(uint64_t cid, const uint8_t *buf, uint32_t size) = 0;
};

// ============================================================================
// Stream — active HTTP/2 stream with send capabilities
// ============================================================================

/** @brief Represents an HTTP/2 stream within a connection.
 *
 *  Streams are active objects: they can both read incoming data/headers
 *  and send outgoing frames. Stream instances are created via
 *  Transport::CreateStream() and delivered to EventHandler callbacks.
 *
 *  Sending:
 *    stream->SendHeaders({{":method","GET"}, {":path","/"}}, true);
 *
 *  Reading data:
 *    uint32_t size = stream->DataSize();
 *    std::vector<uint8_t> buf(size);
 *    stream->ReadData(buf.data(), size);
 */
class Stream {
public:
    virtual ~Stream() {}

    // === Read-only state ===

    /** @brief Get the connection identifier this stream belongs to. */
    virtual uint64_t ConnectionId() const = 0;

    /** @brief Get the HTTP/2 stream identifier. */
    virtual uint32_t StreamId() const = 0;

    /** @brief Get the frame flags associated with the current event.
     *  @return Bitmask of Http2FrameFlag values.
     */
    virtual int Flags() const = 0;

    /** @brief Get the error code from a RST_STREAM frame.
     *  @return The error code, or 0 if no error.
     */
    virtual uint32_t ErrorCode() const = 0;

    /** @brief Get the current stream state.
     *  @return One of the Http2StreamState values.
     */
    virtual int CurrentState() const = 0;

    // === Sending operations ===

    /** @brief Send a HEADERS frame with the given header key-value pairs.
     *
     *  For requests, include pseudo-headers (:method, :path, :scheme, :authority).
     *  For responses, include :status.
     *
     *  @param headers     Vector of (name, value) header pairs.
     *  @param end_stream  If true, sets END_STREAM flag (no body follows).
     *  @return true if the frame was sent successfully.
     */
    virtual bool SendHeaders(const std::vector<std::pair<std::string, std::string>> &headers,
                             bool end_stream = false) = 0;

    /** @brief Send a DATA frame with body data.
     *
     *  Large payloads are automatically split into multiple frames respecting
     *  the negotiated MAX_FRAME_SIZE.
     *
     *  @param data        Pointer to the body data.
     *  @param size        Length of the data in bytes.
     *  @param end_stream  If true, sets END_STREAM flag on the last frame.
     *  @return true if the data was sent successfully.
     */
    virtual bool SendData(const uint8_t *data, uint32_t size,
                          bool end_stream = false) = 0;

    /** @brief Send trailing headers (a second HEADERS frame with END_STREAM).
     *
     *  Commonly used in gRPC to send status information (grpc-status, grpc-message).
     *  This automatically sets END_STREAM on the frame.
     *
     *  @param headers Vector of (name, value) header pairs.
     *  @return true if the trailing headers were sent successfully.
     */
    virtual bool SendTrailingHeaders(
        const std::vector<std::pair<std::string, std::string>> &headers) = 0;

    /** @brief Send a RST_STREAM frame to immediately terminate this stream.
     *  @param error_code The error code (e.g., Http2ErrorCode::Cancel).
     *  @return true if the frame was sent successfully.
     */
    virtual bool SendRSTStream(uint32_t error_code) = 0;

    // === Data reading ===

    /** @brief Get the total number of data bytes currently buffered.
     *  @return Number of bytes available in the data buffer.
     */
    virtual uint32_t DataSize() const = 0;

    /** @brief Copy data out of the stream's data buffer and consume it.
     *
     *  @param buffer Destination buffer (must have at least `size` bytes).
     *  @param size   Maximum bytes to read.
     *  @return Actual bytes copied (may be less than `size` if less data available).
     */
    virtual uint32_t ReadData(uint8_t *buffer, uint32_t size) = 0;

    /** @brief Get a zero-copy pointer to the buffered data without consuming it.
     *
     *  The pointer is valid until the next call to ReceivedData() on the
     *  owning Transport, or until the stream is destroyed.
     *
     *  @param out_size If non-null, receives the number of bytes available.
     *  @return Pointer to the first buffered data slice, or nullptr if empty.
     */
    virtual const uint8_t *PeekData(uint32_t *out_size) const = 0;

    // === Header reading ===

    /** @brief Get the decoded headers as a vector of key-value pairs.
     *
     *  Returns a reference to the internal storage. Valid until the stream
     *  is destroyed or headers are updated.
     *
     *  @return Vector of (name, value) pairs in the order received.
     */
    virtual const std::vector<std::pair<std::string, std::string>> &GetHeaders() const = 0;
};

// ============================================================================
// EventHandler — callback interface for incoming events
// ============================================================================

/** @brief Callback interface for incoming HTTP/2 frame events.
 *
 *  Implement this interface to react to HTTP/2 frames decoded by the transport.
 *  All callbacks are invoked synchronously from within Transport::ReceivedData().
 *  Implementations should not block or perform expensive operations in these callbacks.
 *
 *  Only OnStreamHeaders and OnStreamData are pure virtual (must be implemented).
 *  All other callbacks have default empty implementations.
 */
class EventHandler {
public:
    virtual ~EventHandler() {}

    /** @brief Called when a HEADERS or PUSH_PROMISE frame is fully received.
     *
     *  If the HEADERS frame did not have the END_HEADERS flag, this is called
     *  only after all CONTINUATION frames have been received.
     *
     *  @param stream  The stream associated with the headers.
     *                 Use stream->GetHeaders() to access decoded headers.
     */
    virtual void OnStreamHeaders(std::shared_ptr<Stream> stream) = 0;

    /** @brief Called when a DATA frame is received.
     *
     *  Use stream->DataSize() / stream->ReadData() / stream->PeekData()
     *  to access the payload.
     *
     *  @param stream The stream carrying the data.
     */
    virtual void OnStreamData(std::shared_ptr<Stream> stream) = 0;

    /** @brief Called when a stream transitions to the Closed state.
     *
     *  This is triggered by RST_STREAM (local or remote), or by both sides
     *  sending END_STREAM. Use this to clean up stream-related resources.
     *
     *  @param stream     The stream that was closed.
     *  @param error_code The RST_STREAM error code, or 0 for normal close.
     */
    virtual void OnStreamClosed(std::shared_ptr<Stream> stream, uint32_t error_code) {}

    /** @brief Called when a SETTINGS frame (or SETTINGS ACK) is received.
     *
     *  For a regular SETTINGS frame, `settings` contains all parameter
     *  id/value pairs. For a SETTINGS ACK, `settings` is empty and `ack` is true.
     *
     *  @param cid       Connection identifier.
     *  @param settings  Vector of (setting_id, value) pairs.
     *  @param ack       true if this is a SETTINGS acknowledgment.
     */
    virtual void OnSettings(uint64_t cid,
                            const std::vector<std::pair<uint16_t, uint32_t>> &settings,
                            bool ack) {}

    /** @brief Called when a PING frame is received.
     *  @param cid  Connection identifier.
     *  @param data The 8-byte opaque data from the PING frame.
     *  @param ack  true if this is a PING acknowledgment (ACK flag set).
     */
    virtual void OnPing(uint64_t cid, uint64_t data, bool ack) {}

    /** @brief Called when a GOAWAY frame is received.
     *
     *  GOAWAY signals that the peer is shutting down the connection. After this
     *  callback, no new streams should be created on this connection.
     *
     *  @param cid             Connection identifier.
     *  @param last_stream_id  The highest stream ID the peer will process.
     *  @param error_code      The error code indicating the reason for shutdown.
     *  @param debug           Optional human-readable debug information.
     */
    virtual void OnGoAway(uint64_t cid, uint32_t last_stream_id, uint32_t error_code,
                          const std::string &debug) {}

    /** @brief Called when an async SendRawData operation completes.
     *
     *  Only called when SendRawData returned 0 (async queued).
     *  For synchronous sends (return >0), no callback is issued.
     *
     *  @param cid     Connection identifier.
     *  @param success true if the data was successfully sent, false on error.
     */
    virtual void OnSendComplete(uint64_t cid, bool success) {}
};

// ============================================================================
// FlowControlHandler — optional custom flow control
// ============================================================================

/** @brief Output struct for WINDOW_UPDATE decisions. */
typedef struct {
    uint32_t connection_window_size_increment; /**< Bytes to add to the connection-level window. */
    uint32_t stream_window_size_increment;     /**< Bytes to add to the stream-level window. */
} WindowUpdate;

/** @brief Optional callback interface for custom HTTP/2 flow control.
 *
 *  If not set on the Transport, automatic flow control is used:
 *  received data immediately triggers WINDOW_UPDATE of the same size.
 *
 *  Implement this interface only if you need backpressure or custom
 *  window management (e.g., for proxies or gateways).
 */
class FlowControlHandler {
public:
    virtual ~FlowControlHandler() {}

    /** @brief Called after data has been received on a stream.
     *  Default: automatically sends WINDOW_UPDATE for the same amount.
     *  @param cid        Connection identifier.
     *  @param stream_id  Stream identifier (0 for connection-level).
     *  @param recv_bytes Number of bytes received.
     *  @return WindowUpdate with increments. Return {recv_bytes, recv_bytes} for automatic.
     */
    virtual WindowUpdate OnDataReceived(uint64_t cid, uint32_t stream_id, uint32_t recv_bytes) {
        return {recv_bytes, recv_bytes};
    }

    /** @brief Called when a WINDOW_UPDATE frame is received from the peer.
     *  Default: no-op.
     *  @param cid               Connection identifier.
     *  @param stream_id         Stream identifier (0 for connection-level).
     *  @param window_update_size Increment in bytes.
     */
    virtual void OnWindowUpdate(uint64_t cid, uint32_t stream_id, uint32_t window_update_size) {}

    /** @brief Called before sending data to check the send window.
     *  Default: allows the send (returns {send_bytes, send_bytes}).
     *  @param cid        Connection identifier.
     *  @param stream_id  Stream identifier.
     *  @param send_bytes Bytes about to be sent.
     *  @return WindowUpdate with increments. Return {send_bytes, send_bytes} to allow.
     */
    virtual WindowUpdate OnPreSendData(uint64_t cid, uint32_t stream_id, uint32_t send_bytes) {
        return {send_bytes, send_bytes};
    }
};

// ============================================================================
// ConnectionInfo — snapshot of connection-level state
// ============================================================================

/** @brief Snapshot of connection-level state. */
struct ConnectionInfo {
    uint32_t active_streams;           /**< Number of open/half-closed streams. */
    uint32_t last_stream_id;           /**< Highest remote stream ID seen. */
    bool received_goaway;              /**< Whether GOAWAY has been received. */
    bool sent_goaway;                  /**< Whether GOAWAY has been sent. */
    int32_t connection_window;         /**< Current connection-level send window. */
};

// ============================================================================
// Transport — core HTTP/2 connection interface
// ============================================================================

/** @brief Core HTTP/2 transport interface.
 *
 *  Each Transport instance manages the HTTP/2 protocol state for a single TCP
 *  connection. It handles frame parsing, serialization, HPACK encoding/decoding,
 *  stream lifecycle, and flow control.
 *
 *  Thread Safety: A Transport instance is NOT thread-safe. All methods must be
 *  called from the same thread or protected by external synchronization.
 *
 *  Reentrancy: EventHandler callbacks are invoked synchronously from within
 *  ReceivedData(). Sending data from within a callback (e.g., calling
 *  stream->SendHeaders() in OnStreamHeaders) is supported but may trigger
 *  re-entrant calls to SendService::SendRawData(). If your I/O layer is
 *  not reentrant-safe, defer sends to after ReceivedData() returns (e.g.,
 *  queue them and send in the next event loop iteration).
 *
 *  Lifecycle:
 *  1. Create with CreateTransport()
 *  2. Set handler via SetEventHandler()
 *  3. Feed incoming data via ReceivedData() — returns bytes consumed
 *  4. Create streams via CreateStream() and send via stream methods
 *  5. Call Shutdown() when the TCP connection is closing
 *  6. Release the unique_ptr to destroy
 */
class Transport {
public:
    virtual ~Transport() = default;

    /** @brief Set the event handler. Must not be nullptr for normal operation.
     *  @param handler Pointer to the handler. Caller retains ownership;
     *                 the handler must outlive the transport.
     */
    virtual void SetEventHandler(EventHandler *handler) = 0;

    /** @brief Set a custom flow control handler (optional).
     *
     *  If not called, automatic flow control is used (WINDOW_UPDATE matches
     *  received data size). Call this only if you need custom window management.
     *
     *  @param handler Pointer to the handler, or nullptr to revert to automatic.
     *                 Caller retains ownership; the handler must outlive the transport.
     */
    virtual void SetFlowControlHandler(FlowControlHandler *handler) = 0;

    /** @brief Get the connection identifier associated with this transport.
     *  @return The connection id passed to CreateTransport().
     */
    virtual uint64_t GetConnectionId() const = 0;

    /** @brief Whether this transport was created as a client-side connection.
     *  @return true if client-side, false if server-side.
     */
    virtual bool IsClientSide() const = 0;

    /** @brief Allocate a new HTTP/2 stream.
     *
     *  For client-side transports, stream IDs are odd (1, 3, 5, ...).
     *  For server-side, stream IDs are even (2, 4, 6, ...).
     *
     *  @return A shared_ptr to the new Stream, or nullptr if allocation failed
     *          (max stream ID reached or GOAWAY received).
     */
    virtual std::shared_ptr<Stream> CreateStream() = 0;

    /** @brief Send a SETTINGS frame with the given parameters.
     *  @param settings Vector of (setting_id, value) pairs.
     *  @return true if the SETTINGS frame was sent successfully.
     */
    virtual bool SendSettings(const std::vector<std::pair<uint16_t, uint32_t>> &settings) = 0;

    /** @brief Send a PING frame.
     *  @param info 8-byte opaque data to include in the PING frame.
     *  @return true if the PING frame was sent successfully.
     */
    virtual bool SendPing(uint64_t info) = 0;

    /** @brief Send a GOAWAY frame to initiate graceful connection shutdown.
     *  @param error_code     The error code (e.g., Http2ErrorCode::NoError for graceful).
     *  @param last_stream_id The highest stream ID the sender will process (0 = auto).
     *  @param debug          Optional human-readable debug data.
     *  @return true if the GOAWAY frame was sent successfully.
     */
    virtual bool SendGoAway(uint32_t error_code, uint32_t last_stream_id = 0,
                            const std::string &debug = "") = 0;

    /** @brief Send a PUSH_PROMISE frame to initiate a server push.
     *
     *  Only valid on server-side transports. The promised stream ID is
     *  allocated automatically.
     *
     *  @param request_stream  The client-initiated stream this push is for.
     *  @param headers         Request headers for the promised resource.
     *  @return The promised Stream, or nullptr on failure.
     */
    virtual std::shared_ptr<Stream> SendPushPromise(
        std::shared_ptr<Stream> request_stream,
        const std::vector<std::pair<std::string, std::string>> &headers) = 0;

    /** @brief Feed incoming TCP data to the transport for processing.
     *
     *  The data must start at an HTTP/2 frame boundary (or the client preface
     *  for server-side transports). The transport processes complete frames and
     *  invokes EventHandler callbacks synchronously.
     *
     *  Note: This method invokes EventHandler callbacks synchronously. If your
     *  callbacks call send methods (SendHeaders, SendData, etc.), those will
     *  trigger SendRawData calls reentrantly. Use SetBufferedMode(true) before
     *  ReceivedData() and Flush() after to batch all outgoing frames.
     *
     *  @param buf Pointer to the incoming TCP data buffer.
     *  @param len Number of bytes available in the buffer.
     *  @return The number of bytes consumed (>= 0), or -1 on protocol error.
     *          The caller must retain any unconsumed bytes for the next call.
     */
    virtual int ReceivedData(const uint8_t *buf, uint32_t len) = 0;

    /** @brief Notify the transport that the underlying connection is shutting down.
     *
     *  Sends a GOAWAY frame and cleans up internal state.
     */
    virtual void Shutdown() = 0;

    /** @brief Look up a stream by its identifier.
     *
     *  Useful for server-side code that needs to respond on a specific stream
     *  without holding a reference from the callback.
     *
     *  @param stream_id The HTTP/2 stream identifier.
     *  @return A shared_ptr to the Stream, or nullptr if not found.
     */
    virtual std::shared_ptr<Stream> FindStream(uint32_t stream_id) = 0;

    /** @brief Get a remote SETTINGS value by parameter ID.
     *  @param id The settings parameter identifier (Http2SettingsId).
     *  @return The current value negotiated with the remote peer.
     */
    virtual uint32_t GetRemoteSetting(uint16_t id) const = 0;

    /** @brief Get a local SETTINGS value by parameter ID.
     *  @param id The settings parameter identifier (Http2SettingsId).
     *  @return The current local setting value.
     */
    virtual uint32_t GetLocalSetting(uint16_t id) const = 0;

    /** @brief Get a snapshot of connection-level state. */
    virtual ConnectionInfo GetConnectionInfo() const = 0;

    /** @brief Enable or disable send buffering.
     *
     *  When enabled, frame serialization accumulates data in an internal buffer
     *  instead of calling SendRawData immediately. Call Flush() to send
     *  accumulated data. This reduces the number of small TCP writes.
     *
     *  Default: disabled (each frame is sent immediately).
     *
     *  Typical usage for async I/O:
     *
     *    transport->SetBufferedMode(true);
     *    int consumed = transport->ReceivedData(buf, nread);
     *    transport->Flush();
     *    transport->SetBufferedMode(false);
     *
     *  @param enable true to enable buffering, false to disable.
     */
    virtual void SetBufferedMode(bool enable) = 0;

    /** @brief Send all accumulated buffered data via SendRawData.
     *
     *  If buffered mode is disabled, this is a no-op (data is already sent).
     *  Typically called after a batch of frame operations:
     *
     *    transport->SetBufferedMode(true);
     *    stream->SendHeaders(headers, false);
     *    stream->SendData(body, len, true);
     *    transport->Flush();
     *    transport->SetBufferedMode(false);
     *
     *  @return true if all data was sent successfully, false on error.
     */
    virtual bool Flush() = 0;

    /** @brief Send a WINDOW_UPDATE frame for connection or stream flow control.
     *
     *  @param stream_id  0 for connection-level, or a specific stream ID.
     *  @param increment  Number of bytes to add to the flow control window.
     *  @return true if the WINDOW_UPDATE was sent successfully.
     */
    virtual bool SendWindowUpdate(uint32_t stream_id, uint32_t increment) = 0;
};

// ============================================================================
// Factory
// ============================================================================

/** @brief Create a new HTTP/2 transport instance.
 *
 *  @param connection_id An opaque identifier for this connection, passed through
 *                       to SendService::SendRawData() and EventHandler callbacks.
 *  @param client_side   true for client-initiated connections, false for server-accepted.
 *  @param service       Pointer to the SendService implementation for output.
 *                       Caller retains ownership; the service must outlive the transport.
 *  @return A unique_ptr to the new Transport.
 */
std::unique_ptr<Transport> CreateTransport(uint64_t connection_id, bool client_side, SendService *service);

}  // namespace http2
