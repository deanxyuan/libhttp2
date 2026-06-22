/** @file parser.h
 *  @brief HTTP/2 frame payload parsers (RFC 7540, Section 6).
 *
 *  Each parse function extracts the frame-type-specific fields from a raw
 *  payload buffer into the corresponding frame struct. The common 9-byte
 *  frame header must already be parsed via http2_frame_header_unpack().
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "src/http2/frame.h"

/** @brief Parse a DATA frame payload.
 *  @param hdr    Parsed frame header (already unpacked).
 *  @param input  Pointer to the frame payload (after the 9-byte header).
 *  @param output Output DATA frame structure.
 *  @return Http2ErrorCode value (NoError on success).
 */
int parse_http2_frame_data(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_data *output);

/** @brief Parse a HEADERS frame payload.
 *  @param hdr    Parsed frame header.
 *  @param input  Pointer to the frame payload.
 *  @param output Output HEADERS frame structure.
 *  @return Http2ErrorCode value (NoError on success).
 */
int parse_http2_frame_headers(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_headers *output);

/** @brief Parse a PRIORITY frame payload.
 *  @param hdr    Parsed frame header.
 *  @param input  Pointer to the frame payload.
 *  @param output Output PRIORITY frame structure.
 *  @return Http2ErrorCode value (NoError on success).
 */
int parse_http2_frame_priority(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_priority *output);

/** @brief Parse a RST_STREAM frame payload.
 *  @param hdr    Parsed frame header.
 *  @param input  Pointer to the frame payload.
 *  @param output Output RST_STREAM frame structure.
 *  @return Http2ErrorCode value (NoError on success).
 */
int parse_http2_frame_rst_stream(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_rst_stream *output);

/** @brief Parse a SETTINGS frame payload.
 *  @param hdr    Parsed frame header.
 *  @param input  Pointer to the frame payload.
 *  @param output Output SETTINGS frame structure.
 *  @return Http2ErrorCode value (NoError on success).
 */
int parse_http2_frame_settings(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_settings *output);

/** @brief Parse a PUSH_PROMISE frame payload.
 *  @param hdr    Parsed frame header.
 *  @param input  Pointer to the frame payload.
 *  @param output Output PUSH_PROMISE frame structure.
 *  @return Http2ErrorCode value (NoError on success).
 */
int parse_http2_frame_push_promise(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_push_promise *output);

/** @brief Parse a PING frame payload.
 *  @param hdr    Parsed frame header.
 *  @param input  Pointer to the frame payload.
 *  @param output Output PING frame structure.
 *  @return Http2ErrorCode value (NoError on success).
 */
int parse_http2_frame_ping(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_ping *output);

/** @brief Parse a GOAWAY frame payload.
 *  @param hdr    Parsed frame header.
 *  @param input  Pointer to the frame payload.
 *  @param output Output GOAWAY frame structure.
 *  @return Http2ErrorCode value (NoError on success).
 */
int parse_http2_frame_goaway(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_goaway *output);

/** @brief Parse a WINDOW_UPDATE frame payload.
 *  @param hdr    Parsed frame header.
 *  @param input  Pointer to the frame payload.
 *  @param output Output WINDOW_UPDATE frame structure.
 *  @return Http2ErrorCode value (NoError on success).
 */
int parse_http2_frame_window_update(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_window_update *output);

/** @brief Parse a CONTINUATION frame payload.
 *  @param hdr    Parsed frame header.
 *  @param input  Pointer to the frame payload.
 *  @param output Output CONTINUATION frame structure.
 *  @return Http2ErrorCode value (NoError on success).
 */
int parse_http2_frame_continuation(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_continuation *output);
