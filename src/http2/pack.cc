/** @file pack.cc
 *  @brief HTTP/2 frame serialization (pack) implementations.
 *
 *  Each function serializes a frame struct into a wire-format byte buffer
 *  including the 9-byte common frame header and the frame-type-specific payload.
 */
#include "src/http2/pack.h"
#include <assert.h>
#include <string.h>
#include "src/utils/byte_order.h"

slice_buffer pack_http2_frame_data(http2_frame_data *frame, uint32_t max_frame_size) {
    http2_frame_hdr hdr = frame->hdr;
    hdr.flags &= ~static_cast<uint8_t>(Http2FrameFlag::Padded);
    uint8_t end_stream_flag = hdr.flags & static_cast<uint8_t>(Http2FrameFlag::EndStream);
    uint32_t readed_length = 0;

    slice_buffer buffer;
    while (readed_length < frame->data.size()) {

        uint32_t remain_bytes = frame->data.size() - readed_length;
        hdr.length = (remain_bytes < max_frame_size) ? remain_bytes : max_frame_size;
        bool is_last = (readed_length + hdr.length >= frame->data.size());

        // Only set END_STREAM on the last chunk
        if (is_last) {
            hdr.flags |= end_stream_flag;
        } else {
            hdr.flags &= ~static_cast<uint8_t>(Http2FrameFlag::EndStream);
        }

        slice data = MakeSliceByLength(hdr.length + HTTP2_FRAME_HEADER_SIZE);
        uint8_t *ptr = const_cast<uint8_t *>(data.data());

        http2_frame_header_pack(ptr, &hdr);
        ptr += HTTP2_FRAME_HEADER_SIZE;

        memcpy(ptr, frame->data.data() + readed_length, hdr.length);
        readed_length += hdr.length;

        buffer.add_slice(data);
    }
    return buffer;
}

slice pack_http2_frame_headers(http2_frame_headers *frame) {
    size_t frame_length = HTTP2_FRAME_HEADER_SIZE + frame->hdr.length;
    slice frame_data = MakeSliceByLength(frame_length);

    uint8_t *p = const_cast<uint8_t *>(frame_data.data());
    http2_frame_header_pack(p, &frame->hdr);
    p += HTTP2_FRAME_HEADER_SIZE;

    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::Padded)) {
        *p++ = frame->pad_len;
    }
    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::Priority)) {
        uint32_t stream_id = frame->pspec.depend_stream_id;
        if (frame->pspec.exclusive) {
            stream_id |= (1 << 31);
        }

        put_uint32_in_be_stream(p, stream_id);
        p += 4;
        *p++ = static_cast<uint8_t>(frame->pspec.weight - 1);
    }

    memcpy(p, frame->header_block_fragment.data(), frame->header_block_fragment.size());
    if (frame->pad_len > 0) {
        memset(p + frame->header_block_fragment.size(), 0, frame->pad_len);
    }
    return frame_data;
}

slice pack_http2_frame_priority(http2_frame_priority *frame) {
    constexpr size_t frame_length = HTTP2_FRAME_HEADER_SIZE + 5;

    slice frame_data = MakeSliceByLength(frame_length);

    uint8_t *p = const_cast<uint8_t *>(frame_data.data());
    http2_frame_header_pack(p, &frame->hdr);
    p += HTTP2_FRAME_HEADER_SIZE;

    uint32_t stream_id = frame->pspec.depend_stream_id;
    if (frame->pspec.exclusive) {
        stream_id |= (1 << 31);
    }

    put_uint32_in_be_stream(p, stream_id);
    p += 4;

    if (frame->pspec.weight != 0) {
        *p++ = static_cast<uint8_t>(frame->pspec.weight - 1);
    } else {
        *p++ = 0;
    }

    return frame_data;
}

slice pack_http2_frame_rst_stream(http2_frame_rst_stream *frame) {
    constexpr size_t frame_length = HTTP2_FRAME_HEADER_SIZE + 4;

    slice frame_data = MakeSliceByLength(frame_length);

    uint8_t *p = const_cast<uint8_t *>(frame_data.data());
    http2_frame_header_pack(p, &frame->hdr);
    p += HTTP2_FRAME_HEADER_SIZE;
    put_uint32_in_be_stream(p, frame->error_code);

    return frame_data;
}

slice pack_http2_frame_settings(http2_frame_settings *frame) {
    size_t frame_length = HTTP2_FRAME_HEADER_SIZE + frame->hdr.length;
    slice frame_data = MakeSliceByLength(frame_length);

    uint8_t *p = const_cast<uint8_t *>(frame_data.data());
    http2_frame_header_pack(p, &frame->hdr);
    p += HTTP2_FRAME_HEADER_SIZE;

    for (size_t i = 0; i < frame->settings.size(); i++) {
        put_uint16_in_be_stream(p, frame->settings.at(i).id);
        p += 2;
        put_uint32_in_be_stream(p, frame->settings.at(i).value);
        p += 4;
    }
    return frame_data;
}

slice pack_http2_frame_push_promise(http2_frame_push_promise *frame) {
    size_t frame_length = HTTP2_FRAME_HEADER_SIZE + frame->hdr.length;
    slice frame_data = MakeSliceByLength(frame_length);

    uint8_t *p = const_cast<uint8_t *>(frame_data.data());
    http2_frame_header_pack(p, &frame->hdr);
    p += HTTP2_FRAME_HEADER_SIZE;

    if (frame->hdr.flags & static_cast<uint8_t>(Http2FrameFlag::Padded)) {
        *p++ = frame->pad_len;
    }

    put_uint32_in_be_stream(p, frame->promised_stream_id);
    p += 4;

    memcpy(p, frame->header_block_fragment.data(), frame->header_block_fragment.size());
    if (frame->pad_len > 0) {
        memset(p + frame->header_block_fragment.size(), 0, frame->pad_len);
    }
    return frame_data;
}

slice pack_http2_frame_ping(http2_frame_ping *frame) {
    constexpr size_t frame_length = HTTP2_FRAME_HEADER_SIZE + 8;
    slice frame_data = MakeSliceByLength(frame_length);

    uint8_t *p = const_cast<uint8_t *>(frame_data.data());
    http2_frame_header_pack(p, &frame->hdr);
    p += HTTP2_FRAME_HEADER_SIZE;
    memcpy(p, frame->opaque_data, 8);
    return frame_data;
}

slice pack_http2_frame_goaway(http2_frame_goaway *frame) {
    size_t frame_length = HTTP2_FRAME_HEADER_SIZE + frame->hdr.length;
    slice frame_data = MakeSliceByLength(frame_length);

    uint8_t *p = const_cast<uint8_t *>(frame_data.data());
    http2_frame_header_pack(p, &frame->hdr);
    p += HTTP2_FRAME_HEADER_SIZE;

    put_uint32_in_be_stream(p, frame->last_stream_id);
    p += 4;
    put_uint32_in_be_stream(p, frame->error_code);
    p += 4;

    if (!frame->debug_data.empty()) {
        memcpy(p, frame->debug_data.data(), frame->debug_data.size());
    }
    return frame_data;
}

slice pack_http2_frame_window_update(http2_frame_window_update *frame) {
    constexpr size_t frame_length = HTTP2_FRAME_HEADER_SIZE + 4;
    slice frame_data = MakeSliceByLength(frame_length);

    uint8_t *p = const_cast<uint8_t *>(frame_data.data());
    http2_frame_header_pack(p, &frame->hdr);
    p += HTTP2_FRAME_HEADER_SIZE;

    put_uint32_in_be_stream(p, frame->window_size_inc);

    return frame_data;
}

slice pack_http2_frame_continuation(http2_frame_continuation *frame) {
    size_t frame_length = HTTP2_FRAME_HEADER_SIZE + frame->hdr.length;
    slice frame_data = MakeSliceByLength(frame_length);

    uint8_t *p = const_cast<uint8_t *>(frame_data.data());
    http2_frame_header_pack(p, &frame->hdr);
    p += HTTP2_FRAME_HEADER_SIZE;

    if (!frame->header_block_fragment.empty()) {
        memcpy(p, frame->header_block_fragment.data(), frame->header_block_fragment.size());
    }
    return frame_data;
}
