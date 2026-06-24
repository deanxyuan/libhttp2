#include <string.h>
#include "src/http2/pack.h"
#include "src/http2/parser.h"
#include "src/utils/testutil.h"
#include "src/utils/slice_buffer.h"

class TestPack {};
class RoundTrip {};

TEST(TestPack, FrameHeaders) {
    uint8_t buff[] = {0x00, 0x00, 0x6b, 0x01, 0x04, 0x00, 0x00, 0x00, 0x01, 0x88, 0x40, 0x0c, 0x63, 0x6f, 0x6e,
                      0x74, 0x65, 0x6e, 0x74, 0x2d, 0x74, 0x79, 0x70, 0x65, 0x10, 0x61, 0x70, 0x70, 0x6c, 0x69,
                      0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x2f, 0x67, 0x72, 0x70, 0x63, 0x40, 0x14, 0x67, 0x72,
                      0x70, 0x63, 0x2d, 0x61, 0x63, 0x63, 0x65, 0x70, 0x74, 0x2d, 0x65, 0x6e, 0x63, 0x6f, 0x64,
                      0x69, 0x6e, 0x67, 0x15, 0x69, 0x64, 0x65, 0x6e, 0x74, 0x69, 0x74, 0x79, 0x2c, 0x64, 0x65,
                      0x66, 0x6c, 0x61, 0x74, 0x65, 0x2c, 0x67, 0x7a, 0x69, 0x70, 0x40, 0x0f, 0x61, 0x63, 0x63,
                      0x65, 0x70, 0x74, 0x2d, 0x65, 0x6e, 0x63, 0x6f, 0x64, 0x69, 0x6e, 0x67, 0x0d, 0x69, 0x64,
                      0x65, 0x6e, 0x74, 0x69, 0x74, 0x79, 0x2c, 0x67, 0x7a, 0x69, 0x70};

    http2_frame_headers headers = {};
    http2_frame_hdr *hdr = &headers.hdr;

    hdr->flags = 0x04;
    hdr->type = 0x1;
    hdr->length = 107;
    hdr->stream_id = 1;
    uint8_t tmp[] = {0x88, 0x40, 0x0c, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x74, 0x79, 0x70, 0x65, 0x10,
                     0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x2f, 0x67, 0x72, 0x70, 0x63,
                     0x40, 0x14, 0x67, 0x72, 0x70, 0x63, 0x2d, 0x61, 0x63, 0x63, 0x65, 0x70, 0x74, 0x2d, 0x65, 0x6e,
                     0x63, 0x6f, 0x64, 0x69, 0x6e, 0x67, 0x15, 0x69, 0x64, 0x65, 0x6e, 0x74, 0x69, 0x74, 0x79, 0x2c,
                     0x64, 0x65, 0x66, 0x6c, 0x61, 0x74, 0x65, 0x2c, 0x67, 0x7a, 0x69, 0x70, 0x40, 0x0f, 0x61, 0x63,
                     0x63, 0x65, 0x70, 0x74, 0x2d, 0x65, 0x6e, 0x63, 0x6f, 0x64, 0x69, 0x6e, 0x67, 0x0d, 0x69, 0x64,
                     0x65, 0x6e, 0x74, 0x69, 0x74, 0x79, 0x2c, 0x67, 0x7a, 0x69, 0x70};
    headers.header_block_fragment = MakeStaticSlice(tmp, sizeof(tmp));
    ASSERT_EQ(headers.header_block_fragment.size(), 107);
    slice slice_buf = pack_http2_frame_headers(&headers);
    ASSERT_EQ(memcmp(slice_buf.data(), buff, slice_buf.size()), 0);
}

TEST(TestPack, FrameData) {
    uint8_t buff[] = {0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x1a,
                      0x0a, 0x18, 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x61, 0x20, 0x6d, 0x65,
                      0x73, 0x73, 0x61, 0x67, 0x65, 0x20, 0x74, 0x65, 0x78, 0x74, 0x3a, 0x31};
    http2_frame_data data;
    http2_frame_hdr *hdr = &data.hdr;

    hdr->flags = 0;
    hdr->type = 0;
    hdr->length = 31;
    hdr->stream_id = 1;
    uint8_t tmp[] = {0x00, 0x00, 0x00, 0x00, 0x1a, 0x0a, 0x18, 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x61,
                     0x20, 0x6d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x20, 0x74, 0x65, 0x78, 0x74, 0x3a, 0x31};
    data.data = MakeStaticSlice(tmp, sizeof(tmp));
    slice_buffer slice_buf = pack_http2_frame_data(&data, 1 << 23);
    ASSERT_EQ(slice_buf.slice_count(), 1);
    ASSERT_EQ(memcmp(slice_buf.front().data(), buff, slice_buf.front().size()), 0);
}

TEST(TestPack, FrameData2) {
    uint8_t buff[] = {0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x1a,
                      0x0a, 0x18, 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x61, 0x20, 0x6d, 0x65,
                      0x73, 0x73, 0x61, 0x67, 0x65, 0x20, 0x74, 0x65, 0x78, 0x74, 0x3a, 0x31};
    http2_frame_data data;
    http2_frame_hdr *hdr = &data.hdr;

    hdr->flags = 0;
    hdr->type = 0;
    hdr->length = 31;
    hdr->stream_id = 1;
    uint8_t tmp[] = {0x00, 0x00, 0x00, 0x00, 0x1a, 0x0a, 0x18, 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x61,
                     0x20, 0x6d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x20, 0x74, 0x65, 0x78, 0x74, 0x3a, 0x31};
    data.data = MakeStaticSlice(tmp, sizeof(tmp));
    slice_buffer slice_buf = pack_http2_frame_data(&data, 16);
    ASSERT_EQ(slice_buf.slice_count(), 2);
}

TEST(TestPack, FramePing) {
    uint8_t buff[] = {0x00, 0x00, 0x08, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    http2_frame_ping ping;
    http2_frame_hdr *hdr = &ping.hdr;

    hdr->flags = 0x01;
    hdr->type = 0x6;
    hdr->length = 0x8;
    hdr->stream_id = 0;
    *(uint64_t *)ping.opaque_data = 0x0;
    slice slice_buf = pack_http2_frame_ping(&ping);
    ASSERT_EQ(memcmp(slice_buf.data(), buff, sizeof(buff)), 0);
}

TEST(TestPack, FrameRST) {
    uint8_t buff[] = {0x00, 0x00, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};

    http2_frame_rst_stream rst;
    http2_frame_hdr *hdr = &rst.hdr;
    hdr->length = 4;
    hdr->flags = 0;
    hdr->type = 3;
    hdr->stream_id = 1;
    rst.error_code = 0;
    slice slice_buf = pack_http2_frame_rst_stream(&rst);
    ASSERT_EQ(memcmp(slice_buf.data(), buff, slice_buf.size()), 0);
}

TEST(TestPack, FrameSettings) {
    // SETTINGS frame with 2 entries:
    //   HEADER_TABLE_SIZE(1) = 4096
    //   MAX_FRAME_SIZE(5) = 16384
    uint8_t buff[] = {0x00, 0x00, 0x0c, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x01, 0x00, 0x00, 0x10, 0x00,
                      0x00, 0x05, 0x00, 0x00, 0x40, 0x00};

    http2_frame_settings frame;
    http2_frame_hdr *hdr = &frame.hdr;
    hdr->length = 12;
    hdr->type = 0x04;
    hdr->flags = 0;
    hdr->stream_id = 0;

    http2_settings_entry e1;
    e1.id = 1;
    e1.value = 4096;
    http2_settings_entry e2;
    e2.id = 5;
    e2.value = 16384;
    frame.settings.push_back(e1);
    frame.settings.push_back(e2);

    slice slice_buf = pack_http2_frame_settings(&frame);
    ASSERT_EQ(slice_buf.size(), sizeof(buff));
    ASSERT_EQ(memcmp(slice_buf.data(), buff, sizeof(buff)), 0);
}

TEST(TestPack, FrameGoaway) {
    // GOAWAY frame: last_stream_id=5, error_code=0, no debug data
    uint8_t buff[] = {0x00, 0x00, 0x08, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00};

    http2_frame_goaway frame;
    http2_frame_hdr *hdr = &frame.hdr;
    hdr->length = 8;
    hdr->type = 0x07;
    hdr->flags = 0;
    hdr->stream_id = 0;
    frame.last_stream_id = 5;
    frame.error_code = 0;
    frame.reserved = 0;

    slice slice_buf = pack_http2_frame_goaway(&frame);
    ASSERT_EQ(slice_buf.size(), sizeof(buff));
    ASSERT_EQ(memcmp(slice_buf.data(), buff, sizeof(buff)), 0);
}

TEST(TestPack, FrameWindowUpdate) {
    // WINDOW_UPDATE frame: window_size_inc=1024, stream_id=0
    uint8_t buff[] = {0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x04, 0x00};

    http2_frame_window_update frame;
    http2_frame_hdr *hdr = &frame.hdr;
    hdr->length = 4;
    hdr->type = 0x08;
    hdr->flags = 0;
    hdr->stream_id = 0;
    frame.window_size_inc = 1024;
    frame.reserved = 0;

    slice slice_buf = pack_http2_frame_window_update(&frame);
    ASSERT_EQ(slice_buf.size(), sizeof(buff));
    ASSERT_EQ(memcmp(slice_buf.data(), buff, sizeof(buff)), 0);
}

TEST(TestPack, FramePriority) {
    // PRIORITY frame: stream_id=1, depend_stream_id=3, exclusive=0, weight=16
    uint8_t buff[] = {0x00, 0x00, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
                      0x00, 0x00, 0x00, 0x03, 0x0F};

    http2_frame_priority frame;
    http2_frame_hdr *hdr = &frame.hdr;
    hdr->length = 5;
    hdr->type = 0x02;
    hdr->flags = 0;
    hdr->stream_id = 1;
    frame.pspec.depend_stream_id = 3;
    frame.pspec.exclusive = 0;
    frame.pspec.weight = 16;

    slice slice_buf = pack_http2_frame_priority(&frame);
    ASSERT_EQ(slice_buf.size(), sizeof(buff));
    ASSERT_EQ(memcmp(slice_buf.data(), buff, sizeof(buff)), 0);
}

TEST(TestPack, FrameContinuation) {
    // CONTINUATION frame: stream_id=1, 3-byte header block fragment
    uint8_t frag[] = {0xAA, 0xBB, 0xCC};
    uint8_t buff[] = {0x00, 0x00, 0x03, 0x09, 0x00, 0x00, 0x00, 0x00, 0x01,
                      0xAA, 0xBB, 0xCC};

    http2_frame_continuation frame;
    http2_frame_hdr *hdr = &frame.hdr;
    hdr->length = 3;
    hdr->type = 0x09;
    hdr->flags = 0;
    hdr->stream_id = 1;
    frame.header_block_fragment = MakeStaticSlice(frag, sizeof(frag));

    slice slice_buf = pack_http2_frame_continuation(&frame);
    ASSERT_EQ(slice_buf.size(), sizeof(buff));
    ASSERT_EQ(memcmp(slice_buf.data(), buff, sizeof(buff)), 0);
}

TEST(RoundTrip, HeadersFrame) {
    // Build a HEADERS frame with END_HEADERS flag
    uint8_t tmp[] = {0x88, 0x40, 0x0c, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74,
                     0x2d, 0x74, 0x79, 0x70, 0x65, 0x10, 0x61, 0x70, 0x70, 0x6c,
                     0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x2f, 0x67, 0x72,
                     0x70, 0x63};

    http2_frame_headers original = {};
    original.hdr.length = sizeof(tmp);
    original.hdr.type = 0x01;
    original.hdr.flags = 0x04;
    original.hdr.stream_id = 1;
    original.header_block_fragment = MakeStaticSlice(tmp, sizeof(tmp));

    // Pack
    slice packed = pack_http2_frame_headers(&original);
    ASSERT_EQ(packed.size(), HTTP2_FRAME_HEADER_SIZE + sizeof(tmp));

    // Parse
    http2_frame_hdr parsed_hdr;
    http2_frame_header_unpack(&parsed_hdr, packed.data());

    http2_frame_headers parsed;
    parse_http2_frame_headers(&parsed_hdr, packed.data() + HTTP2_FRAME_HEADER_SIZE, &parsed);

    // Verify
    ASSERT_EQ(parsed.hdr.type, original.hdr.type);
    ASSERT_EQ(parsed.hdr.flags, original.hdr.flags);
    ASSERT_EQ(parsed.hdr.stream_id, original.hdr.stream_id);
    ASSERT_EQ(parsed.hdr.length, original.hdr.length);
    ASSERT_EQ(parsed.header_block_fragment.size(), original.header_block_fragment.size());
    ASSERT_EQ(memcmp(parsed.header_block_fragment.data(), original.header_block_fragment.data(),
                     original.header_block_fragment.size()),
              0);
}

TEST(RoundTrip, DataFrame) {
    // Build a DATA frame with END_STREAM flag
    uint8_t tmp[] = {0x01, 0x02, 0x03, 0x04, 0x05};

    http2_frame_data original = {};
    original.hdr.length = sizeof(tmp);
    original.hdr.type = 0x00;
    original.hdr.flags = 0x01;
    original.hdr.stream_id = 1;
    original.data = MakeStaticSlice(tmp, sizeof(tmp));

    // Pack
    slice_buffer packed = pack_http2_frame_data(&original, 1 << 23);
    ASSERT_EQ(packed.slice_count(), 1);

    // Parse
    const slice &s = packed.front();
    http2_frame_hdr parsed_hdr;
    http2_frame_header_unpack(&parsed_hdr, s.data());

    http2_frame_data parsed;
    parse_http2_frame_data(&parsed_hdr, s.data() + HTTP2_FRAME_HEADER_SIZE, &parsed);

    // Verify
    ASSERT_EQ(parsed.hdr.type, original.hdr.type);
    ASSERT_EQ(parsed.hdr.flags, original.hdr.flags);
    ASSERT_EQ(parsed.hdr.stream_id, original.hdr.stream_id);
    ASSERT_EQ(parsed.hdr.length, original.hdr.length);
    ASSERT_EQ(parsed.data.size(), original.data.size());
    ASSERT_EQ(memcmp(parsed.data.data(), original.data.data(), original.data.size()), 0);
}

TEST(RoundTrip, SettingsFrame) {
    // Build a SETTINGS frame with 3 entries
    http2_frame_settings original = {};
    original.hdr.type = 0x04;
    original.hdr.flags = 0;
    original.hdr.stream_id = 0;

    http2_settings_entry e1;
    e1.id = 1;
    e1.value = 4096;
    http2_settings_entry e2;
    e2.id = 5;
    e2.value = 16384;
    http2_settings_entry e3;
    e3.id = 4;
    e3.value = 65535;
    original.settings.push_back(e1);
    original.settings.push_back(e2);
    original.settings.push_back(e3);
    original.hdr.length = 6 * original.settings.size();

    // Pack
    slice packed = pack_http2_frame_settings(&original);
    ASSERT_EQ(packed.size(), HTTP2_FRAME_HEADER_SIZE + original.hdr.length);

    // Parse
    http2_frame_hdr parsed_hdr;
    http2_frame_header_unpack(&parsed_hdr, packed.data());

    http2_frame_settings parsed;
    parse_http2_frame_settings(&parsed_hdr, packed.data() + HTTP2_FRAME_HEADER_SIZE, &parsed);

    // Verify
    ASSERT_EQ(parsed.hdr.type, original.hdr.type);
    ASSERT_EQ(parsed.hdr.flags, original.hdr.flags);
    ASSERT_EQ(parsed.hdr.stream_id, original.hdr.stream_id);
    ASSERT_EQ(parsed.hdr.length, original.hdr.length);
    ASSERT_EQ(parsed.settings.size(), original.settings.size());
    for (size_t i = 0; i < original.settings.size(); i++) {
        ASSERT_EQ(parsed.settings[i].id, original.settings[i].id);
        ASSERT_EQ(parsed.settings[i].value, original.settings[i].value);
    }
}

int main(int argc, char *argv[]) {
    return test::RunAllTests();
}
