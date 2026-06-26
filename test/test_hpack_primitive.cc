#include <string.h>
#include <string>
#include "src/hpack/decode.h"
#include "src/hpack/encode.h"
#include "src/hpack/metadata.h"
#include "src/utils/testutil.h"
#include "src/utils/useful.h"

class HpackPrimitiveTest {};

// Helper: encode a uint32 value, then decode it back and verify round-trip.
static void round_trip_uint32(uint32_t value, uint8_t prefix_bits) {
    uint8_t mask = INT_MASK(prefix_bits);
    slice encoded = hpack::encode_uint16(value, mask);
    ASSERT_GE(encoded.size(), 1u);

    uint32_t decoded = 0;
    const uint8_t *end = encoded.data() + encoded.size();
    const uint8_t *result = hpack::decode_uint32(encoded.data(), end, decoded, mask);
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(result, end);
    ASSERT_EQ(decoded, value);
}

// ============================================================
// Variable-length integer encoding/decoding round-trip
// ============================================================

TEST(HpackPrimitiveTest, EncodeDecodeUint32_Prefix7) {
    // 7-bit prefix is the most common in HPACK (mask = 127)
    uint32_t values[] = {0, 1, 126, 127, 128, 255, 300, 16383, 16384, 65535};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        round_trip_uint32(values[i], 7);
    }
}

TEST(HpackPrimitiveTest, EncodeDecodeUint32_Prefix1) {
    // 1-bit prefix (mask = 1): only value 0 fits in single byte
    uint32_t values[] = {0, 1, 2, 127, 128, 255, 300, 16383};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        round_trip_uint32(values[i], 1);
    }
}

TEST(HpackPrimitiveTest, EncodeDecodeUint32_Prefix2) {
    // 2-bit prefix (mask = 3)
    uint32_t values[] = {0, 1, 3, 4, 127, 128, 300};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        round_trip_uint32(values[i], 2);
    }
}

TEST(HpackPrimitiveTest, EncodeDecodeUint32_Prefix3) {
    // 3-bit prefix (mask = 7)
    uint32_t values[] = {0, 1, 7, 8, 127, 128, 500};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        round_trip_uint32(values[i], 3);
    }
}

TEST(HpackPrimitiveTest, EncodeDecodeUint32_Prefix4) {
    // 4-bit prefix (mask = 15): used for key index in without-indexing
    uint32_t values[] = {0, 1, 14, 15, 16, 127, 128, 300};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        round_trip_uint32(values[i], 4);
    }
}

TEST(HpackPrimitiveTest, EncodeDecodeUint32_Prefix5) {
    // 5-bit prefix (mask = 31): used for dynamic table size update
    uint32_t values[] = {0, 1, 30, 31, 32, 127, 128, 500};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        round_trip_uint32(values[i], 5);
    }
}

TEST(HpackPrimitiveTest, EncodeDecodeUint32_Prefix6) {
    // 6-bit prefix (mask = 63)
    uint32_t values[] = {0, 1, 62, 63, 64, 127, 128};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        round_trip_uint32(values[i], 6);
    }
}

TEST(HpackPrimitiveTest, EncodeDecodeUint32_Prefix8) {
    // 8-bit prefix (mask = 255): full byte prefix
    uint32_t values[] = {0, 1, 254, 255, 256, 300, 16383, 65535};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        round_trip_uint32(values[i], 8);
    }
}

// Verify specific encoded byte patterns for 7-bit prefix
TEST(HpackPrimitiveTest, EncodeUint16_SpecificBytes_Prefix7) {
    // value=0: single byte, 0 < mask(127) => [0x00]
    slice s0 = hpack::encode_uint16(0, INT_MASK(7));
    ASSERT_EQ(s0.size(), 1u);
    ASSERT_EQ(s0.data()[0], 0x00);

    // value=126: single byte, 126 < 127 => [0x7e]
    slice s126 = hpack::encode_uint16(126, INT_MASK(7));
    ASSERT_EQ(s126.size(), 1u);
    ASSERT_EQ(s126.data()[0], 0x7e);

    // value=127: 127 == mask, multi-byte => [0x7f, 0x00]
    slice s127 = hpack::encode_uint16(127, INT_MASK(7));
    ASSERT_EQ(s127.size(), 2u);
    ASSERT_EQ(s127.data()[0], 0x7f);
    ASSERT_EQ(s127.data()[1], 0x00);

    // value=128: I=128-127=1, => [0x7f, 0x01]
    slice s128 = hpack::encode_uint16(128, INT_MASK(7));
    ASSERT_EQ(s128.size(), 2u);
    ASSERT_EQ(s128.data()[0], 0x7f);
    ASSERT_EQ(s128.data()[1], 0x01);
}

// ============================================================
// String encoding/decoding
// ============================================================

TEST(HpackPrimitiveTest, ParseString_Raw) {
    // Raw (non-Huffman) string "hello": [length=5, H=0] [h e l l o]
    uint8_t buf[] = {0x05, 'h', 'e', 'l', 'l', 'o'};
    std::string result;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::parse_string(result, buf, end);
    ASSERT_TRUE(ret != nullptr);
    ASSERT_EQ(result, "hello");
    ASSERT_EQ(ret, end);
}

TEST(HpackPrimitiveTest, ParseString_Empty) {
    // Empty string: length = 0
    uint8_t buf[] = {0x00};
    std::string result;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::parse_string(result, buf, end);
    ASSERT_TRUE(ret != nullptr);
    ASSERT_TRUE(result.empty());
    ASSERT_EQ(ret, end);
}

TEST(HpackPrimitiveTest, ParseString_ShortAscii) {
    // Single character "x"
    uint8_t buf[] = {0x01, 'x'};
    std::string result;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::parse_string(result, buf, end);
    ASSERT_TRUE(ret != nullptr);
    ASSERT_EQ(result, "x");
    ASSERT_EQ(ret, end);
}

TEST(HpackPrimitiveTest, ParseString_LongerValue) {
    // Longer value: "application/json" (16 bytes)
    uint8_t buf[] = {0x10, 'a', 'p', 'p', 'l', 'i', 'c', 'a', 't',
                     'i',  'o', 'n', '/', 'j', 's', 'o', 'n'};
    std::string result;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::parse_string(result, buf, end);
    ASSERT_TRUE(ret != nullptr);
    ASSERT_EQ(result, "application/json");
    ASSERT_EQ(ret, end);
}

TEST(HpackPrimitiveTest, ParseStringKey_Lowercase) {
    // Valid lowercase key "content-type" (12 bytes)
    uint8_t buf[] = {0x0c, 'c', 'o', 'n', 't', 'e', 'n', 't', '-',
                     't',  'y', 'p', 'e'};
    std::string result;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::parse_string_key(result, buf, end);
    ASSERT_TRUE(ret != nullptr);
    ASSERT_EQ(result, "content-type");
}

TEST(HpackPrimitiveTest, ParseStringKey_RejectsUppercase) {
    // Uppercase in key "Content-Type" -- must be rejected per RFC 7541 Section 5.2
    uint8_t buf[] = {0x0c, 'C', 'o', 'n', 't', 'e', 'n', 't', '-',
                     'T',  'y', 'p', 'e'};
    std::string result;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::parse_string_key(result, buf, end);
    ASSERT_TRUE(ret == nullptr);
}

TEST(HpackPrimitiveTest, ParseStringKey_EmptyKey) {
    // Empty key is valid (no uppercase to reject)
    uint8_t buf[] = {0x00};
    std::string result;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::parse_string_key(result, buf, end);
    ASSERT_TRUE(ret != nullptr);
    ASSERT_TRUE(result.empty());
}

TEST(HpackPrimitiveTest, ParseStringKey_AllLowercase) {
    // All-lowercase key "host" should succeed
    uint8_t buf[] = {0x04, 'h', 'o', 's', 't'};
    std::string result;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::parse_string_key(result, buf, end);
    ASSERT_TRUE(ret != nullptr);
    ASSERT_EQ(result, "host");
}

// ============================================================
// HPACK representation encoding
// ============================================================

TEST(HpackPrimitiveTest, EncodeIndex_SmallIndex) {
    // Indexed header field with index=1
    // Format: [1 | index(7+)] => high bit set, lower 7 bits = index
    slice s = hpack::encode_index(1);
    ASSERT_GE(s.size(), 1u);
    ASSERT_EQ(s.data()[0], 0x81);  // 0x80 | 1
}

TEST(HpackPrimitiveTest, EncodeIndex_LargerIndex) {
    // Index=62 fits in 7-bit prefix
    slice s = hpack::encode_index(62);
    ASSERT_GE(s.size(), 1u);
    ASSERT_EQ(s.data()[0], static_cast<uint8_t>(0x80 | 62));
}

TEST(HpackPrimitiveTest, EncodeIndex_MultiByte) {
    // Index=200 exceeds 7-bit prefix (127), needs multi-byte
    slice s = hpack::encode_index(200);
    ASSERT_GE(s.size(), 2u);
    // First byte: 0x80 | 127 = 0xff
    ASSERT_EQ(s.data()[0], 0xff);
    // Continuation: 200 - 127 = 73, no more continuation
    ASSERT_EQ(s.data()[1], 73);
}

TEST(HpackPrimitiveTest, EncodeWithIncrementalIndexing) {
    hpack::mdelem_data mdel;
    mdel.key = slice("name", 4);
    mdel.value = slice("value", 5);

    slice s = hpack::encode_with_incremental_indexing(mdel);
    ASSERT_TRUE(s.size() > 0);

    // Type byte: 0x40 (incremental indexing)
    ASSERT_EQ(s.data()[0], 0x40);
    // Key length: 4 (H=0)
    ASSERT_EQ(s.data()[1], 0x04);
    // Key content
    ASSERT_EQ(memcmp(s.data() + 2, "name", 4), 0);
    // Value length: 5 (H=0)
    ASSERT_EQ(s.data()[6], 0x05);
    // Value content
    ASSERT_EQ(memcmp(s.data() + 7, "value", 5), 0);
    // Total: 1 + 1 + 4 + 1 + 5 = 12
    ASSERT_EQ(s.size(), 12u);
}

TEST(HpackPrimitiveTest, EncodeWithoutIndexing_LiteralKV) {
    hpack::mdelem_data mdel;
    mdel.key = slice("host", 4);
    mdel.value = slice("example.com", 11);

    slice s = hpack::encode_without_indexing(mdel);
    ASSERT_TRUE(s.size() > 0);

    // Type byte: 0x00 (no indexing, literal key)
    ASSERT_EQ(s.data()[0], 0x00);
    // Key length: 4
    ASSERT_EQ(s.data()[1], 0x04);
    // Key content
    ASSERT_EQ(memcmp(s.data() + 2, "host", 4), 0);
    // Value length: 11
    ASSERT_EQ(s.data()[6], 0x0b);
    // Value content
    ASSERT_EQ(memcmp(s.data() + 7, "example.com", 11), 0);
}

TEST(HpackPrimitiveTest, EncodeWithoutIndexing_KeyIndex) {
    hpack::mdelem_data mdel;
    mdel.key = slice("unused", 6);
    mdel.value = slice("val", 3);

    // key_index=1, 4-bit prefix: 1 < 15, single byte
    slice s = hpack::encode_without_indexing(mdel, 1);
    ASSERT_TRUE(s.size() > 0);
    // First byte: key_index=1 in 4-bit prefix => 0x01
    ASSERT_EQ(s.data()[0], 0x01);
    // Value length: 3
    ASSERT_EQ(s.data()[1], 0x03);
    // Value content
    ASSERT_EQ(memcmp(s.data() + 2, "val", 3), 0);
    // Total: 1 + 1 + 3 = 5
    ASSERT_EQ(s.size(), 5u);
}

TEST(HpackPrimitiveTest, EncodeWithoutIndexing_KeyIndex_MultiByte) {
    hpack::mdelem_data mdel;
    mdel.key = slice("unused", 6);
    mdel.value = slice("v", 1);

    // key_index=20, exceeds 4-bit prefix (15), needs multi-byte
    slice s = hpack::encode_without_indexing(mdel, 20);
    ASSERT_TRUE(s.size() > 0);
    // First byte: 0x0f (mask), second byte: 20-15=5
    ASSERT_EQ(s.data()[0], 0x0f);
    ASSERT_EQ(s.data()[1], 0x05);
    // Value length: 1
    ASSERT_EQ(s.data()[2], 0x01);
    // Value content
    ASSERT_EQ(s.data()[3], 'v');
}

TEST(HpackPrimitiveTest, EncodeNeverIndexed) {
    hpack::mdelem_data mdel;
    mdel.key = slice("secret", 6);
    mdel.value = slice("token", 5);

    slice s = hpack::encode_never_indexed(mdel);
    ASSERT_TRUE(s.size() > 0);

    // Type byte: 0x10 (never indexed)
    ASSERT_EQ(s.data()[0], 0x10);
    // Key length: 6
    ASSERT_EQ(s.data()[1], 0x06);
    // Key content
    ASSERT_EQ(memcmp(s.data() + 2, "secret", 6), 0);
    // Value length: 5
    ASSERT_EQ(s.data()[8], 0x05);
    // Value content
    ASSERT_EQ(memcmp(s.data() + 9, "token", 5), 0);
    // Total: 1 + 1 + 6 + 1 + 5 = 14
    ASSERT_EQ(s.size(), 14u);
}

TEST(HpackPrimitiveTest, EncodeUpdateMaxSize) {
    // Dynamic table size update, 5-bit prefix
    slice s = hpack::encode_update_max_size(0);
    ASSERT_GE(s.size(), 1u);
    // Type bits: 001 in bits 5-7 => 0x20
    // value=0 < 31(mask), so first byte = 0x20 | 0 = 0x20
    ASSERT_EQ(s.data()[0], 0x20);
}

// ============================================================
// Malformed input handling
// ============================================================

TEST(HpackPrimitiveTest, DecodeUint32_TruncatedSingleByte) {
    // For prefix=7, value 128 encodes as [0x7f, 0x01].
    // Provide only the first byte (mask value) -- should fail.
    uint8_t buf[] = {0x7f};
    uint32_t dst = 0;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::decode_uint32(buf, end, dst, INT_MASK(7));
    ASSERT_TRUE(ret == nullptr);
}

TEST(HpackPrimitiveTest, DecodeUint32_TruncatedContinuation) {
    // Multi-byte encoding with continuation bit set but no final byte.
    // For prefix=7, value 300 encodes as [0x7f, 0xad, 0x01].
    // 0xad = 10101101 has high bit set (continuation). Truncating at 2 bytes.
    uint8_t buf[] = {0x7f, 0xad};
    uint32_t dst = 0;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::decode_uint32(buf, end, dst, INT_MASK(7));
    ASSERT_TRUE(ret == nullptr);
}

TEST(HpackPrimitiveTest, ParseString_TruncatedBuffer) {
    // Length byte says 10 bytes follow, but only 3 bytes available.
    uint8_t buf[] = {0x0a, 'a', 'b', 'c'};
    std::string result;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::parse_string(result, buf, end);
    ASSERT_TRUE(ret == nullptr);
}

TEST(HpackPrimitiveTest, ParseString_EmptyBufferRange) {
    // Empty buffer range (buf == buf_end).
    std::string result;
    uint8_t dummy = 0;
    const uint8_t *ret = hpack::parse_string(result, &dummy, &dummy);
    ASSERT_TRUE(ret == nullptr);
}

TEST(HpackPrimitiveTest, ParseStringKey_TruncatedBuffer) {
    // Key length claims 20 bytes, but only 5 available.
    uint8_t buf[] = {0x14, 'a', 'b', 'c', 'd', 'e'};
    std::string result;
    const uint8_t *end = buf + sizeof(buf);
    const uint8_t *ret = hpack::parse_string_key(result, buf, end);
    ASSERT_TRUE(ret == nullptr);
}

int main(int argc, char *argv[]) {
    return test::RunAllTests();
}
