/** @file frame.cc
 *  @brief Implementation of HTTP/2 frame header pack/unpack and frame builders.
 */
#include "src/http2/frame.h"
#include <string.h>
#include "src/utils/byte_order.h"

/** @brief Serialize an HTTP/2 frame header into a 9-byte wire-format buffer.
 *
 *  Wire format (RFC 7540, Section 4.1):
 *    Bytes 0-2: 24-bit payload length (big-endian)
 *    Byte  3:   8-bit frame type
 *    Byte  4:   8-bit frame flags
 *    Bytes 5-8: 1-bit reserved + 31-bit stream identifier
 */
void http2_frame_header_pack(uint8_t *buf, const http2_frame_hdr *hd) {
    put_uint32_in_be_stream(&buf[0], (uint32_t)(hd->length << 8));
    buf[3] = hd->type;
    buf[4] = hd->flags;
    put_uint32_in_be_stream(&buf[5], (uint32_t)hd->stream_id);
    // ignore hd->reserved for now
}

void http2_frame_header_unpack(http2_frame_hdr *hd, const uint8_t *buf) {
    hd->length = get_uint32_from_be_stream(&buf[0]) >> 8;
    hd->type = buf[3];
    hd->flags = buf[4];
    hd->stream_id = get_uint32_from_be_stream(&buf[5]) & HTTP2_STREAM_ID_MASK;
    hd->reserved = 0;
}

void http2_frame_header_init(http2_frame_hdr *hd, size_t length, uint8_t type, uint8_t flags, uint32_t stream_id) {
    hd->length = length;
    hd->type = type;
    hd->flags = flags;
    hd->stream_id = stream_id;
    hd->reserved = 0;
}

http2_frame_settings build_http2_frame_settings(int flags, std::vector<http2_settings_entry> *settings) {
    http2_frame_settings frame;
    if (flags & static_cast<uint8_t>(Http2FrameFlag::Ack)) {
        http2_frame_header_init(&frame.hdr, 0, static_cast<uint8_t>(Http2FrameType::Settings), flags, 0);
        return frame;
    }
    size_t length = 6 * settings->size();
    http2_frame_header_init(&frame.hdr, length, static_cast<uint8_t>(Http2FrameType::Settings), flags, 0);
    frame.settings = std::move(*settings);
    return frame;
}

http2_frame_ping build_http2_frame_ping(uint8_t *data, bool ack) {
    http2_frame_ping frame;
    uint8_t flags = ack ? static_cast<uint8_t>(Http2FrameFlag::Ack) : 0;
    http2_frame_header_init(&frame.hdr, 8, static_cast<uint8_t>(Http2FrameType::Ping), flags, 0);
    memcpy(frame.opaque_data, data, 8);
    return frame;
}

http2_frame_goaway build_http2_frame_goaway(uint32_t last_stream_id, uint32_t error_code, const slice &debug) {
    http2_frame_goaway frame;
    http2_frame_header_init(&frame.hdr, 8 + debug.size(), static_cast<uint8_t>(Http2FrameType::GoAway), 0, 0);
    frame.last_stream_id = last_stream_id;
    frame.error_code = error_code;
    frame.debug_data = debug;
    frame.reserved = 0;
    return frame;
}

http2_frame_window_update build_http2_frame_window_update(uint32_t stream_id, uint32_t window_size_inc) {
    http2_frame_window_update frame;
    http2_frame_header_init(&frame.hdr, 4, static_cast<uint8_t>(Http2FrameType::WindowUpdate), 0, stream_id);
    frame.reserved = 0;
    frame.window_size_inc = window_size_inc;
    return frame;
}

http2_frame_data build_http2_frame_data(uint32_t stream_id, int flags, const slice &data) {
    http2_frame_data frame;
    http2_frame_header_init(&frame.hdr, data.size(), static_cast<uint8_t>(Http2FrameType::Data), flags, stream_id);
    frame.pad_len = 0;
    frame.data = data;
    return frame;
}

http2_frame_headers build_http2_frame_headers(uint32_t stream_id, int flags, const slice &header_block,
                                              http2_priority_spec *spec) {
    if (!spec) {
        flags &= ~static_cast<uint8_t>(Http2FrameFlag::Priority);
    }

    http2_frame_headers frame;
    size_t length = header_block.size();
    if (flags & static_cast<uint8_t>(Http2FrameFlag::Priority)) {
        frame.pspec = *spec;
        length += 5;
    } else {
        frame.pspec.depend_stream_id = 0;
        frame.pspec.exclusive = 0;
        frame.pspec.weight = 0;
    }
    http2_frame_header_init(&frame.hdr, length, static_cast<uint8_t>(Http2FrameType::Headers), flags, stream_id);
    frame.header_block_fragment = header_block;
    frame.pad_len = 0;
    return frame;
}

http2_frame_push_promise build_http2_frame_push_promise(uint32_t associated_stream_id, uint32_t promised_stream_id, int flags, const slice &header_block) {
    http2_frame_push_promise frame;
    http2_frame_header_init(&frame.hdr, 4 + header_block.size(), static_cast<uint8_t>(Http2FrameType::PushPromise), flags, associated_stream_id);
    frame.pad_len = 0;
    frame.reserved = 0;
    frame.promised_stream_id = promised_stream_id;
    frame.header_block_fragment = header_block;
    return frame;
}

http2_frame_rst_stream build_http2_frame_rst_stream(uint32_t stream_id, int flags, uint32_t error_code) {
    http2_frame_rst_stream frame;
    http2_frame_header_init(&frame.hdr, 4, static_cast<uint8_t>(Http2FrameType::RstStream), flags, stream_id);
    frame.error_code = error_code;
    return frame;
}

http2_frame_priority build_http2_frame_priority(uint32_t stream_id, int flags, const http2_priority_spec &spec) {
    http2_frame_priority frame;
    http2_frame_header_init(&frame.hdr, 5, static_cast<uint8_t>(Http2FrameType::Priority), flags, stream_id);
    frame.pspec = spec;
    return frame;
}
