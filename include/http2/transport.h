#pragma once
#include <stdint.h>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class http2_connection;
namespace http2 {

/* Implemented externally, libhttp2 internal call this interface to send tcp data */
class SendService {
public:
    virtual ~SendService() {}

    //
    // cid:  cid refers to the connection id, managed by the external network module
    // buf:  buf refers to the data pointer to be sent
    // size: size refers to the length of the data to be sent
    virtual bool SendRawData(uint64_t cid, const uint8_t *buf, uint32_t size);
};

// SETTINGS
/**
 * HTTP2_SETTINGS_HEADER_TABLE_SIZE:
 *  Allows the sender to inform the remote endpoint of the maximum
 *  size of the header compression table used to decode header blocks,
 *  in octets.  The encoder can select any size equal to or less than
 *  this value by using signaling specific to the header compression
 *  format inside a header block. The initial value is 4,096 octets.
 */

/**
 * HTTP2_SETTINGS_ENABLE_PUSH:
 *  This setting can be used to disable server push.  An endpoint MUST
 *  NOT send a PUSH_PROMISE frame if it receives this parameter set to
 *  a value of 0.  An endpoint that has both set this parameter to 0
 *  and had it acknowledged MUST treat the receipt of a PUSH_PROMISE
 *  frame as a connection error of type PROTOCOL_ERROR.
 */

/**
 * HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
 *   Indicates the maximum number of concurrent streams that the sender
 *   will allow.  This limit is directional: it applies to the number
 *   of streams that the sender permits the receiver to create.
 *   Initially, there is no limit to this value.  It is recommended
 *   that this value be no smaller than 100, so as to not unnecessarily
 *   limit parallelism.
 */

/**
 * HTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
 *  Indicates the sender’s initial window size (in octets) for
 *  stream-level flow control.  The initial value is 2^16-1 (65,535)
 *  octets.
 */

/**
 * HTTP2_SETTINGS_MAX_FRAME_SIZE:
 *  Indicates the size of the largest frame payload that the sender
 *  is willing to receive, in octets.
 */

/**
 * HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
 *   This advisory setting informs a peer of the maximum size of header
 *   list that the sender is prepared to accept, in octets.  The value
 *   is based on the uncompressed size of header fields, including the
 *   length of the name and value in octets plus an overhead of 32
 *   octets for each header field.
 */

#define HTTP2_SETTINGS_HEADER_TABLE_SIZE 1
#define HTTP2_SETTINGS_ENABLE_PUSH 2
#define HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS 3
#define HTTP2_SETTINGS_INITIAL_WINDOW_SIZE 4
#define HTTP2_SETTINGS_MAX_FRAME_SIZE 5
#define HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE 6

typedef enum {
    HTTP2_CLAMP_INVALID_VALUE,          // clamp
    HTTP2_DISCONNECT_ON_INVALID_VALUE,  // disconnect
} InvalidValueBehavior;

typedef struct {
    const char *name;
    uint32_t default_value;
    uint32_t min_value;
    uint32_t max_value;
    InvalidValueBehavior invalid_value_behavior;
    uint32_t error_value;
} SettingParameters;

#define HTTP2_NUMBER_OF_SETTINGS 7

const extern SettingParameters global_settings_parameters[HTTP2_NUMBER_OF_SETTINGS];

// stream state
#define HTTP2_STREAM_IDLE 0
#define HTTP2_STREAM_RESERVED_LOCAL 1
#define HTTP2_STREAM_RESERVED_REMOTE 2
#define HTTP2_STREAM_OPEN 3
#define HTTP2_STREAM_HALF_CLOSED_LOCAL 4
#define HTTP2_STREAM_HALF_CLOSED_REMOTE 5
#define HTTP2_STREAM_CLOSED 6

#define HTTP2_MAX_STREAM_ID 0x7fffffff

class Stream {
public:
    virtual ~Stream() {}
    virtual uint64_t ConnectionId() = 0;
    virtual uint32_t StreamId() = 0;

    virtual int32_t Weight() = 0;  // Priority
    virtual bool Exclusive() = 0;  // Priority
    virtual int32_t Flags() = 0;
    virtual uint32_t ErrorCode() = 0;
    virtual int CurrentState() = 0;
    virtual std::multimap<std::string, std::string> GetHeaders() = 0;

    /*
     * uint32_t len = GetDataBlock(...);
     * if (len > 0) {
     *     uint8_t* buf = new uint8_t[len];
     *     PopDataBlock(output, len);
     * }
     */
    virtual uint32_t GetDataBlock(uint32_t (*parse_func)(const uint8_t *ptr, uint32_t len)) = 0;
    virtual void PopDataBlock(uint8_t *output, uint32_t size) = 0;
};

class FrameEventHandler {
public:
    virtual ~FrameEventHandler() {}

    virtual void OnData(std::shared_ptr<Stream> stream) = 0;  // FRAME_DATA

    // if PUSH_PROMISE frame, promise = true
    virtual void OnHeaders(std::shared_ptr<Stream> stream, bool promise) = 0;  // FRAME_HEADERS

    virtual void OnPriority(std::shared_ptr<Stream> stream);

    virtual void OnRSTStream(std::shared_ptr<Stream> stream) = 0;  // FRAME_RST_STREAM

    virtual void OnSettings(uint64_t cid, uint16_t id, uint32_t value,
                            bool ack) = 0;  // FRAME_SETTINGS

    virtual void OnPing(uint64_t cid, uint64_t data, bool ack) = 0;  // FRAME_PING

    virtual void OnGoAway(uint64_t cid, uint32_t last_stream_id, uint32_t error_code,
                          const std::string &debug) = 0;  // FRAME_GOAWAY
};

class Request final {
    friend class ::http2_connection;

public:
    Request();
    ~Request();
    void SetStreamId(uint32_t sid);
    void AddMetadata(const std::string &key, const std::string &value);
    void AppendData(const uint8_t *data, uint32_t size);
    void Finish();  // finish stream (flag: END_OF_STREAM)
    void Reset();

private:
    uint32_t stream_id_;
    std::vector<std::pair<std::string, std::string>> headers_;
    std::vector<std::string> data_;
    bool finish_;
};

class Response final {
    friend class ::http2_connection;

public:
    Response();
    ~Response();
    void SetStreamId(uint32_t sid);
    void AppendData(const uint8_t *data, uint32_t size);
    void AddInitlizeMetadata(const std::string &key, const std::string &value);
    void AddTrailingMetadata(const std::string &key, const std::string &value);
    void Finish();  // finish stream (flag: END_OF_STREAM)
    void Reset();

private:
    uint32_t stream_id_;
    std::vector<std::pair<std::string, std::string>> initlize_headers_;
    std::vector<std::pair<std::string, std::string>> trailing_headers_;
    std::vector<std::string> data_;
    bool finish_;
};

typedef struct {
    uint32_t connection_window_size_increment;
    uint32_t stream_window_size_increment;
} WindowUpdate;

class FlowControlHandler {
public:
    virtual ~FlowControlHandler() {}
    virtual WindowUpdate OnDataReceived(uint64_t cid, uint32_t stream_id, uint32_t recv_bytes) = 0;
    virtual void OnWindowUpdate(uint64_t cid, uint32_t stream_id, uint32_t window_update_size) = 0;
    virtual WindowUpdate OnPreSendData(uint64_t cid, uint32_t stream_id, uint32_t send_bytes) = 0;
};

class Transport {
public:
    virtual ~Transport() {}

    virtual void SetFlowControlHandler(FlowControlHandler *handler) = 0;
    virtual void SetFrameEventHandler(FrameEventHandler *handler) = 0;

    virtual uint64_t GetConnectionId() = 0;
    virtual bool IsClientSide() = 0;

    virtual bool SendPing(uint64_t info) = 0;
    virtual bool SendSettings(const std::vector<std::pair<uint16_t, uint32_t>> &settings) = 0;
    virtual bool SendPriority(uint32_t stream_id, uint8_t weight, uint32_t depend_stream_id) = 0;

    virtual bool SendRSTStream(uint32_t stream_id, uint32_t error_code) = 0;
    virtual void SendGoAway(uint32_t last_stream_id, uint32_t error_code, const std::string &debug) = 0;
    virtual bool SendWindowUpdate(uint32_t stream_id, WindowUpdate *wu) = 0;

    virtual bool SendRequest(Request *req) = 0;
    virtual bool SendResponse(Response *rsp) = 0;

    // if initlize_headers not null, send PUSH_PROMISE
    // else just allocate local stream id
    virtual uint32_t CreateStream(std::vector<std::pair<std::string, std::string>> *promise_headers) = 0;

    /*
     * Return -1 means an error occurred, return 0 means still need to provide
     * data, other values greater than 0 means the length of a complete http2
     * packet. When calling the ReceivedData function, at least one complete
     * http2 package must be provided.
     */
    virtual int CheckPackageLength(const uint8_t *data, uint32_t len) = 0;

    /*
     * Need to provide one or more complete http2 data package. To get the
     * complete http2 package, please check CheckHttp2PackageLength function.
     */
    virtual void ReceivedData(const uint8_t *buf, uint32_t len) = 0;

    // Call this function to notify a connection to be disconnected.
    virtual void Shutdown() = 0;
};

// If client_side is false, means the connection comes from API accept.
// If client_side is true, means the connection comes from API connect.
Transport *CreateTransport(uint64_t connection_id, bool client_side, SendService *service);
void DestroyTransport(Transport *transport);

enum LobLevel { kDebug = 0, kInfo = 1, kWarn = 2, kError = 3 };
void SetLogPrintFunction(void (*print_func)(const char *file, int line, int level, const char *message));
void SetLogLevel(int level);
}  // namespace http2
