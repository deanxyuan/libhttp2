/** @file pack.h
 *  @brief HTTP/2 frame serialization (pack) functions.
 *
 *  Each pack function serializes a frame struct into a wire-format byte buffer
 *  suitable for transmission over TCP. The buffer includes the 9-byte common
 *  frame header followed by the frame-type-specific payload.
 */
#pragma once
#include <stdint.h>
#include "src/http2/frame.h"
#include "src/utils/slice_buffer.h"

/** @brief Pack a DATA frame, splitting it into chunks of at most max_frame_size.
 *  @param frame         Pointer to the DATA frame to serialize.
 *  @param max_frame_size Maximum payload size per frame chunk (SETTINGS_MAX_FRAME_SIZE).
 *  @return A slice_buffer containing one or more serialized DATA frame chunks.
 */
slice_buffer pack_http2_frame_data(http2_frame_data *frame, uint32_t max_frame_size);

/** @brief Pack a HEADERS frame into a single wire-format buffer.
 *  @param frame Pointer to the HEADERS frame to serialize.
 *  @return A slice containing the serialized HEADERS frame.
 */
slice pack_http2_frame_headers(http2_frame_headers *frame);

/** @brief Pack a PRIORITY frame into a single wire-format buffer.
 *  @param frame Pointer to the PRIORITY frame to serialize.
 *  @return A slice containing the serialized PRIORITY frame.
 */
slice pack_http2_frame_priority(http2_frame_priority *frame);

/** @brief Pack a RST_STREAM frame into a single wire-format buffer.
 *  @param frame Pointer to the RST_STREAM frame to serialize.
 *  @return A slice containing the serialized RST_STREAM frame.
 */
slice pack_http2_frame_rst_stream(http2_frame_rst_stream *frame);

/** @brief Pack a SETTINGS frame into a single wire-format buffer.
 *  @param frame Pointer to the SETTINGS frame to serialize.
 *  @return A slice containing the serialized SETTINGS frame.
 */
slice pack_http2_frame_settings(http2_frame_settings *frame);

/** @brief Pack a PUSH_PROMISE frame into a single wire-format buffer.
 *  @param frame Pointer to the PUSH_PROMISE frame to serialize.
 *  @return A slice containing the serialized PUSH_PROMISE frame.
 */
slice pack_http2_frame_push_promise(http2_frame_push_promise *frame);

/** @brief Pack a PING frame into a single wire-format buffer.
 *  @param frame Pointer to the PING frame to serialize.
 *  @return A slice containing the serialized PING frame.
 */
slice pack_http2_frame_ping(http2_frame_ping *frame);

/** @brief Pack a GOAWAY frame into a single wire-format buffer.
 *  @param frame Pointer to the GOAWAY frame to serialize.
 *  @return A slice containing the serialized GOAWAY frame.
 */
slice pack_http2_frame_goaway(http2_frame_goaway *frame);

/** @brief Pack a WINDOW_UPDATE frame into a single wire-format buffer.
 *  @param frame Pointer to the WINDOW_UPDATE frame to serialize.
 *  @return A slice containing the serialized WINDOW_UPDATE frame.
 */
slice pack_http2_frame_window_update(http2_frame_window_update *frame);

/** @brief Pack a CONTINUATION frame into a single wire-format buffer.
 *  @param frame Pointer to the CONTINUATION frame to serialize.
 *  @return A slice containing the serialized CONTINUATION frame.
 */
slice pack_http2_frame_continuation(http2_frame_continuation *frame);
