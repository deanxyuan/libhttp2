#pragma once
#include <stdint.h>
#include <vector>
#include <type_traits>
#include "src/utils/slice.h"

typedef enum {
    /**
     * DATA frames convey arbitrary, variable-length sequences of
     *  octets associated with a stream.  One or more DATA frames are used,
     *  for instance, to carry HTTP request or response payloads.
     */
    HTTP2_FRAME_DATA = 0x0,
    /**
     * The HEADERS frame is used to open a stream,
     *  and additionally carries a header block fragment.  HEADERS frames can
     *  be sent on a stream in the "idle", "reserved (local)", "open", or
     *  "half-closed (remote)" state.
     */
    HTTP2_FRAME_HEADERS = 0x1,
    /**
     * The PRIORITY frame specifies the sender-advised priority
     *  of a stream.  It can be sent in any stream state,
     *  including idle or closed streams.
     */
    HTTP2_FRAME_PRIORITY = 0x2,
    /**
     * The RST_STREAM frame allows for immediate termination of a
     *  stream.  RST_STREAM is sent to request cancellation of a stream or to
     *  indicate that an error condition has occurred.
     */
    HTTP2_FRAME_RST_STREAM = 0x3,
    /**
     * The SETTINGS frame conveys configuration parameters that
     *  affect how endpoints communicate, such as preferences and constraints
     *  on peer behavior.  The SETTINGS frame is also used to acknowledge the
     *  receipt of those parameters.  Individually, a SETTINGS parameter can
     *  also be referred to as a "setting".
     */
    HTTP2_FRAME_SETTINGS = 0x4,
    /**
     * The PUSH_PROMISE frame is used to notify the peer endpoint
     *  in advance of streams the sender intends to initiate.  The
     *  PUSH_PROMISE frame includes the unsigned 31-bit identifier of the
     *  stream the endpoint plans to create along with a set of headers that
     *  provide additional context for the stream.
     */
    HTTP2_FRAME_PUSH_PROMISE = 0x5,
    /**
     * The PING frame is a mechanism for measuring a minimal
     *  round-trip time from the sender, as well as determining whether an
     *  idle connection is still functional.  PING frames can be sent from
     *  any endpoint.
     */
    HTTP2_FRAME_PING = 0x6,
    /**
     * The GOAWAY frame is used to initiate shutdown of a
     *  connection or to signal serious error conditions.  GOAWAY allows an
     *  endpoint to gracefully stop accepting new streams while still
     *  finishing processing of previously established streams.  This enables
     *  administrative actions, like server maintenance.
     */
    HTTP2_FRAME_GOAWAY = 0x7,
    /**
     *  The WINDOW_UPDATE frame is used to implement flow control;
     */
    HTTP2_FRAME_WINDOW_UPDATE = 0x8,
    /**
     * The CONTINUATION frame is used to continue a sequence of
     *  header block fragments . Any number of CONTINUATION
     *  frames can be sent, as long as the preceding frame is on the same
     *  stream and is a HEADERS, PUSH_PROMISE, or CONTINUATION frame without
     *  the END_HEADERS flag set.
     */
    HTTP2_FRAME_CONTINUATION = 0x9,
} http2_frame_type;

typedef enum {
    /**
     * When set, bit 0 indicates that this frame is the
     *  last that the endpoint will send for the identified stream.
     */
    HTTP2_FLAG_END_STREAM = 0x1,

    HTTP2_FLAG_ACK = 0x1,
    /**
     * When set, bit 2 indicates that this frame
     *  contains an entire header block and is not followed
     *  by any CONTINUATION frames.
     */
    HTTP2_FLAG_END_HEADERS = 0x4,
    /**
     * When set, bit 3 indicates that the Pad Length field
     *  and any padding that it describes are present.
     */
    HTTP2_FLAG_PADDED = 0x8,
    /**
     *  When set, bit 5 indicates that the Exclusive Flag
     *   (E), Stream Dependency, and Weight fields are present;
     */
    HTTP2_FLAG_PRIORITY = 0x20,
} http2_frame_flags;

typedef struct {
    uint32_t length;  // payload length
    uint8_t type;
    uint8_t flags;
    uint8_t reserved;
    uint32_t stream_id;
} http2_frame_hdr;

typedef struct {
    http2_frame_hdr hdr;
    uint8_t pad_len;
    slice data;
} http2_frame_data;

// Follow the PRIORITY flag to exist
typedef struct {
    uint32_t stream_id;
    uint16_t weight;
    uint8_t exclusive;
} http2_priority_spec;

typedef struct {
    http2_frame_hdr hdr;
    uint8_t pad_len;
    http2_priority_spec pspec;  // with PRIORITY flag
    slice header_block_fragment;
} http2_frame_headers;

typedef struct {
    http2_frame_hdr hdr;
    http2_priority_spec pspec;  // with PRIORITY flag
} http2_frame_priority;

typedef struct {
    http2_frame_hdr hdr;
    uint32_t error_code;
} http2_frame_rst_stream;

typedef struct {
    uint16_t id;
    uint32_t value;
} http2_settings_entry;

typedef struct {
    http2_frame_hdr hdr;
    std::vector<http2_settings_entry> settings;
} http2_frame_settings;

typedef struct {
    http2_frame_hdr hdr;
    uint8_t pad_len;
    uint32_t promised_stream_id;
    slice header_block_fragment;
    uint8_t reserved;
} http2_frame_push_promise;

typedef struct {
    http2_frame_hdr hdr;
    uint8_t opaque_data[8];
} http2_frame_ping;

typedef struct {
    http2_frame_hdr hdr;
    uint32_t last_stream_id;  // 31 bits
    uint32_t error_code;
    slice debug_data;
    uint8_t reserved;
} http2_frame_goaway;

typedef struct {
    http2_frame_hdr hdr;
    uint32_t window_size_inc;  // 31 bits
    uint8_t reserved;
} http2_frame_window_update;

typedef struct {
    http2_frame_hdr hdr;
    slice header_block_fragment;
} http2_frame_continuation;

#define HTTP2_STREAM_ID_MASK 0x7fffffff
#define HTTP2_FRAME_HEADER_SIZE 9

void http2_frame_header_pack(uint8_t *out, const http2_frame_hdr *hd);
void http2_frame_header_unpack(http2_frame_hdr *hd, const uint8_t *input);
void http2_frame_header_init(http2_frame_hdr *hd, size_t length, uint8_t type, uint8_t flags, uint32_t stream_id);

http2_frame_settings build_http2_frame_settings(int flag, std::vector<http2_settings_entry> &settings);
http2_frame_settings build_http2_frame_settings_ack();
http2_frame_ping build_http2_frame_ping(uint8_t payload[8], bool ack);
http2_frame_goaway build_http2_frame_goaway(uint32_t error_code, uint32_t last_stream_id, slice debug);
http2_frame_window_update build_http2_frame_window_update(uint32_t stream_id, uint32_t window_size_inc);
// TODO
http2_frame_data build_http2_frame_data(uint32_t stream_id, const slice &data, int flags);
http2_frame_headers build_http2_frame_headers(uint32_t stream_id, slice_buffer *header_block_fragment, int flags);
http2_frame_push_promise build_http2_frame_push_promise(uint32_t stream_id, slice_buffer *header_block_fragment);
http2_frame_rst_stream build_http2_frame_rst_stream(uint32_t error_code);
http2_frame_priority build_http2_frame_priority(http2_priority_spec *spec);
