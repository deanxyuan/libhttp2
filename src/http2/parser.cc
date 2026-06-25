/** @file parser.cc
 *  @brief HTTP/2 frame payload parser implementations.
 *
 *  Each function extracts frame-type-specific fields from raw payload bytes
 *  into the corresponding frame struct. Frame format diagrams from RFC 7540
 *  are included as inline block comments for reference.
 */
#include "src/http2/parser.h"
#include <string.h>
#include "src/http2/errors.h"
#include "src/utils/byte_order.h"
#include "src/utils/slice.h"

int parse_http2_frame_data(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_data *frame) {
    /*
        +---------------+
        |Pad Length? (8)|
        +---------------+-----------------------------------------------+
        |                            Data (*)                         ...
        +---------------------------------------------------------------+
        |                           Padding (*)                       ...
        +---------------------------------------------------------------+
    */

    frame->hdr = *hdr;

    const uint8_t *data = nullptr;
    int data_size = 0;

    uint8_t pad_length = 0;
    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::Padded)) {
        pad_length = input[0];
        if (static_cast<uint32_t>(pad_length) + 1 > frame->hdr.length) {
            return static_cast<int>(Http2ErrorCode::FrameSizeError);
        }
        data = input + 1;
        data_size = frame->hdr.length - pad_length - 1;  // 1 bytes pad length
    } else {
        data = input;
        data_size = frame->hdr.length;
    }

    frame->data = MakeStaticSlice(data, data_size);
    frame->pad_len = pad_length;

    return static_cast<int>(Http2ErrorCode::NoError);
}

int parse_http2_frame_headers(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_headers *frame) {
    /*
        +---------------+
        |Pad Length? (8)|
        +-+-------------+-----------------------------------------------+
        |E|                 Stream Dependency? (31)                     |
        +-+-------------+-----------------------------------------------+
        |  Weight? (8)  |
        +-+-------------+-----------------------------------------------+
        |                   Header Block Fragment (*)                 ...
        +---------------------------------------------------------------+
        |                           Padding (*)                       ...
        +---------------------------------------------------------------+
    */

    frame->hdr = *hdr;

    const uint8_t *payload = nullptr;
    size_t payload_length = 0;
    uint8_t pad_length = 0;

    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::Padded)) {
        pad_length = input[0];
        if (static_cast<uint32_t>(pad_length) + 1 > frame->hdr.length) {
            return static_cast<int>(Http2ErrorCode::FrameSizeError);
        }
        payload = input + 1;
        payload_length = frame->hdr.length - pad_length - 1;
    } else {
        pad_length = 0;
        payload = input;
        payload_length = frame->hdr.length;
    }

    frame->pad_len = pad_length;

    int32_t dep_stream_id = 0;
    uint8_t exclusive = 0;
    uint16_t weight = 0;

    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::Priority)) {
        dep_stream_id = get_uint32_from_be_stream(payload) & HTTP2_STREAM_ID_MASK;
        exclusive = (payload[0] & 0x80) != 0;
        weight = payload[4] + 1;  // 1-256

        payload += 4 + 1;  // weight + stream id
        payload_length -= 4 + 1;
    }

    frame->pspec.depend_stream_id = dep_stream_id;
    frame->pspec.weight = weight;
    frame->pspec.exclusive = exclusive;
    frame->header_block_fragment = MakeStaticSlice(payload, payload_length);

    return static_cast<int>(Http2ErrorCode::NoError);
}

int parse_http2_frame_priority(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_priority *frame) {
    /*
        +-+-------------------------------------------------------------+
        |E|                  Stream Dependency (31)                     |
        +-+-------------+-----------------------------------------------+
        |   Weight (8)  |
        +-+-------------+
        don't have anty flags
    */
    frame->hdr = *hdr;

    if (frame->hdr.length != 5) {
        return static_cast<int>(Http2ErrorCode::FrameSizeError);
    }
    frame->pspec.depend_stream_id = get_uint32_from_be_stream(input) & HTTP2_STREAM_ID_MASK;
    frame->pspec.exclusive = (input[0] & 0x80) != 0;
    frame->pspec.weight = input[4] + 1;  // 1-256
    return static_cast<int>(Http2ErrorCode::NoError);
}

int parse_http2_frame_rst_stream(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_rst_stream *frame) {
    /*
        +---------------------------------------------------------------+
        |                        Error Code (32)                        |
        +---------------------------------------------------------------+
     */
    frame->hdr = *hdr;

    if (hdr->length != 4) {
        return static_cast<int>(Http2ErrorCode::FrameSizeError);
    }
    frame->error_code = get_uint32_from_be_stream(input);

    return static_cast<int>(Http2ErrorCode::NoError);
}

int parse_http2_frame_settings(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_settings *frame) {
    /*
        +-------------------------------+
        |       Identifier (16)         |
        +-------------------------------+-------------------------------+
        |                        Value (32)                             |
        +---------------------------------------------------------------+
     */
    frame->hdr = *hdr;

    frame->settings.clear();

    if (frame->hdr.length % 6 != 0) {
        return static_cast<int>(Http2ErrorCode::FrameSizeError);
    }

    // uint16_t id + uint32_t value = 6 bytes
    size_t niv = frame->hdr.length / 6;
    const uint8_t *payload = input;

    for (size_t i = 0; i < niv; i++) {
        http2_settings_entry entry;
        entry.id = get_uint16_from_be_stream(payload);
        entry.value = get_uint32_from_be_stream(payload + 2);
        frame->settings.emplace_back(entry);
        payload += 6;
    }

    return static_cast<int>(Http2ErrorCode::NoError);
}

int parse_http2_frame_push_promise(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_push_promise *frame) {
    /*
        +---------------+
        |Pad Length? (8)|
        +-+-------------+-----------------------------------------------+
        |R|                  Promised Stream ID (31)                    |
        +-+-----------------------------+-------------------------------+
        |                   Header Block Fragment (*)                 ...
        +---------------------------------------------------------------+
        |                           Padding (*)                       ...
        +---------------------------------------------------------------+
    */
    frame->hdr = *hdr;

    uint8_t pad_len = 0;
    const uint8_t *payload = nullptr;
    uint32_t payload_size = 0;
    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::Padded)) {
        pad_len = input[0];
        if (static_cast<uint32_t>(pad_len) + 1 > hdr->length) {
            return static_cast<int>(Http2ErrorCode::FrameSizeError);
        }
        payload = input + 1;
        payload_size = hdr->length - pad_len - 1;

    } else {
        payload = input;
        payload_size = hdr->length;
    }

    frame->promised_stream_id = get_uint32_from_be_stream(payload) & HTTP2_STREAM_ID_MASK;
    frame->reserved = (payload[0] & 0x80) != 0;
    frame->pad_len = pad_len;
    frame->header_block_fragment = MakeStaticSlice(payload + 4, payload_size - 4);
    return static_cast<int>(Http2ErrorCode::NoError);
}

int parse_http2_frame_ping(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_ping *frame) {
    /*
        +---------------------------------------------------------------+
        |                                                               |
        |                      Opaque Data (64)                         |
        |                                                               |
        +---------------------------------------------------------------+
    */
    frame->hdr = *hdr;

    if (frame->hdr.length != 8) {
        return static_cast<int>(Http2ErrorCode::FrameSizeError);
    }
    memcpy(frame->opaque_data, input, 8);
    return static_cast<int>(Http2ErrorCode::NoError);
}

int parse_http2_frame_goaway(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_goaway *frame) {
    /*
        +-+-------------------------------------------------------------+
        |R|                  Last-Stream-ID (31)                        |
        +-+-------------------------------------------------------------+
        |                      Error Code (32)                          |
        +---------------------------------------------------------------+
        |                  Additional Debug Data (*)                    |
        +---------------------------------------------------------------+
    */
    frame->hdr = *hdr;

    if (hdr->length < 8) {
        return static_cast<int>(Http2ErrorCode::FrameSizeError);
    }
    frame->reserved = (input[0] & 0x80) != 0;
    frame->last_stream_id = get_uint32_from_be_stream(input) & HTTP2_STREAM_ID_MASK;
    frame->error_code = get_uint32_from_be_stream(input + sizeof(uint32_t));
    frame->debug_data = MakeStaticSlice(input + 8, hdr->length - 8);

    return static_cast<int>(Http2ErrorCode::NoError);
}

int parse_http2_frame_window_update(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_window_update *frame) {
    /*
        +-+-------------------------------------------------------------+
        |R|              Window Size Increment (31)                     |
        +-+-------------------------------------------------------------+
    */
    frame->hdr = *hdr;

    if (frame->hdr.length != 4) {
        return static_cast<int>(Http2ErrorCode::FrameSizeError);
    }
    frame->reserved = (input[0] & 0x80) != 0;
    frame->window_size_inc = get_uint32_from_be_stream(input) & HTTP2_STREAM_ID_MASK;
    return static_cast<int>(Http2ErrorCode::NoError);
}

int parse_http2_frame_continuation(http2_frame_hdr *hdr, const uint8_t *input, http2_frame_continuation *frame) {
    /*
        +---------------------------------------------------------------+
        |                   Header Block Fragment (*)                 ...
        +---------------------------------------------------------------+
        follow HEADERS, IF NOT SET END_HEADERS FLAG
    */
    frame->hdr = *hdr;
    frame->header_block_fragment = MakeStaticSlice(input, hdr->length);
    return static_cast<int>(Http2ErrorCode::NoError);
}

/*
    +-----------------------------------------------+
    |                 Length (24)                   |
    +---------------+---------------+---------------+
    |   Type (8)    |   Flags (8)   |
    +-+-------------+---------------+-------------------------------+
    |R|                 Stream Identifier (31)                      |
    +=+=============================================================+
    |                   Frame Payload (0...)                      ...
    +---------------------------------------------------------------+
*/
