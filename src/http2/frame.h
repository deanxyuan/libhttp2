/** @file frame.h
 *  @brief HTTP/2 frame structures and builder functions (RFC 7540, Section 6).
 *
 *  Defines C structs for each HTTP/2 frame type and utility functions for
 *  initializing, packing, unpacking, and constructing frames. Frame type and
 *  flag enums are defined in http2/transport.h.
 */
#pragma once
#include <stdint.h>
#include <vector>

#include "src/utils/slice.h"
#include "http2/transport.h"

/** @brief Bitmask to extract the 31-bit stream identifier (clears the reserved bit). */
constexpr uint32_t HTTP2_STREAM_ID_MASK = 0x7FFFFFFF;

/** @brief Size in bytes of the common HTTP/2 frame header (9 octets). */
constexpr uint32_t HTTP2_FRAME_HEADER_SIZE = 9;

/** @brief Common 9-byte header shared by all HTTP/2 frame types. */
typedef struct {
    uint32_t length;  /**< 24-bit payload length (excluding the 9-byte header). */
    uint8_t type;     /**< Frame type (cast of Http2FrameType). */
    uint8_t flags;    /**< Frame flags bitmask (cast of Http2FrameFlag). */
    uint8_t reserved; /**< Reserved bit (currently unused, always 0). */
    uint32_t stream_id; /**< 31-bit stream identifier. */
} http2_frame_hdr;

/** @brief DATA frame (RFC 7540, Section 6.1). Carries request or response body data. */
typedef struct {
    http2_frame_hdr hdr;  /**< Frame header. */
    uint8_t pad_len;      /**< Padding length (present if PADDED flag is set). */
    slice data;           /**< Payload data bytes. */
} http2_frame_data;

/** @brief Priority specification used by HEADERS and PRIORITY frames (RFC 7540, Section 5.3). */
typedef struct {
    uint32_t depend_stream_id; /**< Stream dependency identifier. */
    uint16_t weight;           /**< Priority weight (1-256). */
    uint8_t exclusive;         /**< Exclusive flag (1 = exclusive dependency). */
} http2_priority_spec;

/** @brief HEADERS frame (RFC 7540, Section 6.2). Opens a stream and carries header block fragment. */
typedef struct {
    http2_frame_hdr hdr;          /**< Frame header. */
    uint8_t pad_len;              /**< Padding length (present if PADDED flag is set). */
    http2_priority_spec pspec;    /**< Priority info (present if PRIORITY flag is set). */
    slice header_block_fragment;  /**< HPACK-encoded header block. */
} http2_frame_headers;

/** @brief PRIORITY frame (RFC 7540, Section 6.3). Sets or changes stream priority. */
typedef struct {
    http2_frame_hdr hdr;       /**< Frame header. */
    http2_priority_spec pspec; /**< Priority specification for the target stream. */
} http2_frame_priority;

/** @brief RST_STREAM frame (RFC 7540, Section 6.4). Immediately terminates a stream. */
typedef struct {
    http2_frame_hdr hdr;  /**< Frame header. */
    uint32_t error_code;  /**< Error code indicating the reason for reset. */
} http2_frame_rst_stream;

/** @brief A single SETTINGS parameter id/value pair (RFC 7540, Section 6.5). */
typedef struct {
    uint16_t id;    /**< SETTINGS identifier (cast of Http2SettingsId). */
    uint32_t value; /**< Value for this setting. */
} http2_settings_entry;

/** @brief SETTINGS frame (RFC 7540, Section 6.5). Communicates configuration parameters. */
typedef struct {
    http2_frame_hdr hdr;                       /**< Frame header. */
    std::vector<http2_settings_entry> settings; /**< List of setting id/value pairs. */
} http2_frame_settings;

/** @brief PUSH_PROMISE frame (RFC 7540, Section 6.6). Pre-announces a server push stream. */
typedef struct {
    http2_frame_hdr hdr;          /**< Frame header. */
    uint8_t pad_len;              /**< Padding length (present if PADDED flag is set). */
    uint32_t promised_stream_id;  /**< 31-bit promised stream identifier. */
    slice header_block_fragment;  /**< HPACK-encoded request headers for the promised stream. */
    uint8_t reserved;             /**< Reserved bit (currently unused). */
} http2_frame_push_promise;

/** @brief PING frame (RFC 7540, Section 6.7). Measures round-trip time or liveness. */
typedef struct {
    http2_frame_hdr hdr;    /**< Frame header. */
    uint8_t opaque_data[8]; /**< 8-byte opaque data payload. */
} http2_frame_ping;

/** @brief GOAWAY frame (RFC 7540, Section 6.8). Signals graceful shutdown or error. */
typedef struct {
    http2_frame_hdr hdr;     /**< Frame header. */
    uint32_t last_stream_id; /**< 31-bit last stream ID the sender will process. */
    uint32_t error_code;     /**< Error code indicating the reason for shutdown. */
    slice debug_data;        /**< Optional human-readable debug data. */
    uint8_t reserved;        /**< Reserved bit (currently unused). */
} http2_frame_goaway;

/** @brief WINDOW_UPDATE frame (RFC 7540, Section 6.9). Adjusts flow control window. */
typedef struct {
    http2_frame_hdr hdr;       /**< Frame header. */
    uint32_t window_size_inc;  /**< 31-bit window size increment. */
    uint8_t reserved;          /**< Reserved bit (currently unused). */
} http2_frame_window_update;

/** @brief CONTINUATION frame (RFC 7540, Section 6.10). Continues a header block fragment. */
typedef struct {
    http2_frame_hdr hdr;          /**< Frame header. */
    slice header_block_fragment;  /**< Continuation of the HPACK-encoded header block. */
} http2_frame_continuation;

/** @brief Serialize an HTTP/2 frame header into a 9-byte wire-format buffer.
 *  @param out  Destination buffer (must be at least 9 bytes).
 *  @param hd   Frame header structure to serialize.
 */
void http2_frame_header_pack(uint8_t *out, const http2_frame_hdr *hd);

/** @brief Deserialize a 9-byte wire-format buffer into an HTTP/2 frame header.
 *  @param hd    Output frame header structure.
 *  @param input Source buffer (must be at least 9 bytes).
 */
void http2_frame_header_unpack(http2_frame_hdr *hd, const uint8_t *input);

/** @brief Initialize an HTTP/2 frame header with the given fields.
 *  @param hd        Pointer to the frame header to initialize.
 *  @param length    Payload length in bytes.
 *  @param type      Frame type (cast of Http2FrameType).
 *  @param flags     Frame flags bitmask (cast of Http2FrameFlag).
 *  @param stream_id 31-bit stream identifier.
 */
void http2_frame_header_init(http2_frame_hdr *hd, size_t length, uint8_t type, uint8_t flags, uint32_t stream_id);

/** @brief Build a SETTINGS frame from a list of setting entries.
 *  @param flags    Frame flags (e.g., Ack).
 *  @param settings Pointer to vector of setting entries (moved into the frame).
 *  @return Populated SETTINGS frame.
 */
http2_frame_settings build_http2_frame_settings(int flags, std::vector<http2_settings_entry> *settings);

/** @brief Build a PING frame from 8 bytes of opaque data.
 *  @param data 8-byte opaque payload.
 *  @param ack  If true, sets the ACK flag on the PING frame.
 *  @return Populated PING frame.
 */
http2_frame_ping build_http2_frame_ping(uint8_t *data, bool ack);

/** @brief Build a GOAWAY frame with error code and optional debug data.
 *  @param last_stream_id Highest stream ID the sender will process.
 *  @param error_code     Error code indicating the reason for shutdown.
 *  @param debug          Optional human-readable debug data.
 *  @return Populated GOAWAY frame.
 */
http2_frame_goaway build_http2_frame_goaway(uint32_t last_stream_id, uint32_t error_code, const slice &debug);

/** @brief Build a WINDOW_UPDATE frame for a given stream.
 *  @param stream_id       Target stream (0 for connection-level).
 *  @param window_size_inc Window size increment in bytes.
 *  @return Populated WINDOW_UPDATE frame.
 */
http2_frame_window_update build_http2_frame_window_update(uint32_t stream_id, uint32_t window_size_inc);

/** @brief Build a DATA frame carrying body data for a stream.
 *  @param stream_id Target stream identifier.
 *  @param flags     Frame flags (e.g., EndStream).
 *  @param data      Payload data.
 *  @return Populated DATA frame.
 */
http2_frame_data build_http2_frame_data(uint32_t stream_id, int flags, const slice &data);

/** @brief Build a HEADERS frame with an HPACK-encoded header block.
 *  @param stream_id     Target stream identifier.
 *  @param flags         Frame flags (e.g., EndStream, EndHeaders, Priority).
 *  @param header_block  HPACK-encoded header block fragment.
 *  @param spec          Optional priority specification (nullptr if PRIORITY flag is not set).
 *  @return Populated HEADERS frame.
 */
http2_frame_headers build_http2_frame_headers(uint32_t stream_id, int flags, const slice &header_block,
                                              http2_priority_spec *spec);

/** @brief Build a PUSH_PROMISE frame to pre-announce a server push stream.
 *  @param associated_stream_id The stream this promise is associated with.
 *  @param promised_stream_id   The promised stream identifier.
 *  @param flags                Frame flags (e.g., EndHeaders).
 *  @param header_block         HPACK-encoded request headers for the promised stream.
 *  @return Populated PUSH_PROMISE frame.
 */
http2_frame_push_promise build_http2_frame_push_promise(uint32_t associated_stream_id, uint32_t promised_stream_id, int flags, const slice &header_block);

/** @brief Build a RST_STREAM frame to terminate a stream.
 *  @param stream_id  Target stream identifier.
 *  @param flags      Frame flags.
 *  @param error_code Error code indicating the reason for reset.
 *  @return Populated RST_STREAM frame.
 */
http2_frame_rst_stream build_http2_frame_rst_stream(uint32_t stream_id, int flags, uint32_t error_code);

/** @brief Build a PRIORITY frame to set stream priority.
 *  @param stream_id Target stream identifier.
 *  @param flags     Frame flags.
 *  @param spec      Priority specification (dependency and weight).
 *  @return Populated PRIORITY frame.
 */
http2_frame_priority build_http2_frame_priority(uint32_t stream_id, int flags, const http2_priority_spec &spec);
