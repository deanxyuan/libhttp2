#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include "src/hpack/dynamic_metadata.h"
#include "src/hpack/hpack.h"
#include "src/hpack/send_record.h"
#include "src/hpack/static_metadata.h"
#include "src/utils/slice_buffer.h"
#include "src/utils/testutil.h"

class HpackTest {};

// ---------------------------------------------------------------------------
// Helper: parse a hex string into a newly-allocated byte array.
// The caller must delete[] the result.
// ---------------------------------------------------------------------------
static size_t parse_hex(const std::string &hex, uint8_t **output) {
    assert(hex.size() % 2 == 0);
    size_t count = hex.size() / 2;
    *output = new uint8_t[count];
    const char *ptr = hex.data();
    for (size_t i = 0; i < count; i++) {
        uint8_t buf[8] = {0};
        sscanf(ptr, "%02hhx", buf);
        (*output)[i] = buf[0];
        ptr += 2;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Helper: build an mdelem_data from C string literals.
// ---------------------------------------------------------------------------
static hpack::mdelem_data make_md(const char *key, const char *value) {
    hpack::mdelem_data md;
    md.key.assign(key);
    md.value.assign(value);
    return md;
}

// ---------------------------------------------------------------------------
// Helper: verify that a decoded header list matches expected key/value pairs.
// ---------------------------------------------------------------------------
static void verify_headers(const std::vector<hpack::mdelem_data> &decoded,
                           const std::vector<hpack::mdelem_data> &expected) {
    ASSERT_EQ(decoded.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        ASSERT_TRUE(decoded[i].key == expected[i].key);
        ASSERT_TRUE(decoded[i].value == expected[i].value);
    }
}

// ---------------------------------------------------------------------------
// Known gRPC request header blocks (hex-encoded HPACK payloads).
// ---------------------------------------------------------------------------

// req1: first request -- all literal headers with incremental indexing
static const char *kReq1 =
    "8683400a3a617574686f72697479153135302e3135382e3139322e3135303a3530303531"
    "40053a706174682f2f68656c6c6f73747265616d696e67776f726c642e4d756c74694772"
    "65657465722f736179526567756c61724d73674002746508747261696c657273400c636f"
    "6e74656e742d74797065106170706c69636174696f6e2f67727063400a757365722d6167"
    "656e742d677270632d632b2b2f312e33332e3220677270632d632f31332e302e3020286c"
    "696e75783b20636874747032294014677270632d6163636570742d656e636f64696e6715"
    "6964656e746974792c6465666c6174652c677a6970400f6163636570742d656e636f6469"
    "6e670d6964656e746974792c677a6970";

// req2..req6: subsequent requests that reference the dynamic table
static const char *kReq2 =
    "8683c40f342f2f68656c6c6f73747265616d696e67776f726c642e4d756c746947726565"
    "7465722f73617953747265616d50726f63c2c1c0bfbe";

static const char *kReq3 =
    "8683c40f342f2f68656c6c6f73747265616d696e67776f726c642e4d756c746947726565"
    "7465722f73617953747265616d50726f63c2c1c0bfbe";

static const char *kReq4 =
    "8683c40f342f2f68656c6c6f73747265616d696e67776f726c642e4d756c746947726565"
    "7465722f73617953747265616d50726f63c2c1c0bfbe";

static const char *kReq5 =
    "8683c40f342f2f68656c6c6f73747265616d696e67776f726c642e4d756c746947726565"
    "7465722f73617953747265616d50726f63c2c1c0bfbe";

static const char *kReq6 =
    "8683c40f342f2f68656c6c6f73747265616d696e67776f726c642e4d756c746947726565"
    "7465722f736179526567756c61724d7367c2c1c0bfbe";

// ---------------------------------------------------------------------------
// Test: decode a series of gRPC request header blocks using a shared dynamic
// table.  Subsequent requests reference entries added by earlier ones.
// ---------------------------------------------------------------------------
TEST(HpackTest, DecodeMultipleRequests) {
    hpack::dynamic_metadata_table dyn_table(4096);

    // req1 decodes to a set of literal headers with incremental indexing.
    {
        uint8_t *data = nullptr;
        size_t len = parse_hex(kReq1, &data);
        std::vector<hpack::mdelem_data> headers;
        int r = hpack::decode_headers(data, static_cast<uint32_t>(len), &dyn_table,
                                      &headers);
        ASSERT_EQ(r, 0);
        // req1 contains: :method POST, :scheme https, :authority, :path, te,
        // content-type, user-agent, grpc-accept-encoding, accept-encoding
        ASSERT_GE(headers.size(), 7u);
        delete[] data;
    }

    // req2..req6 reference the dynamic table populated by req1.
    const char *requests[] = {kReq2, kReq3, kReq4, kReq5, kReq6};
    for (const char *hex : requests) {
        uint8_t *data = nullptr;
        size_t len = parse_hex(hex, &data);
        std::vector<hpack::mdelem_data> headers;
        int r = hpack::decode_headers(data, static_cast<uint32_t>(len), &dyn_table,
                                      &headers);
        ASSERT_EQ(r, 0);
        ASSERT_GT(headers.size(), 0u);
        delete[] data;
    }
}

// ---------------------------------------------------------------------------
// Test: encode-then-decode round-trip for a typical gRPC request.
// ---------------------------------------------------------------------------
TEST(HpackTest, RoundTrip) {
    hpack::compressor comp;
    hpack::compressor_init(&comp);
    hpack::dynamic_metadata_table dyn_table(4096);

    std::vector<hpack::mdelem_data> original;
    original.push_back(make_md(":method", "POST"));
    original.push_back(make_md(":scheme", "https"));
    original.push_back(make_md(":path", "/grpc.service/Method"));
    original.push_back(make_md(":authority", "example.com:443"));
    original.push_back(make_md("content-type", "application/grpc"));
    original.push_back(make_md("te", "trailers"));
    original.push_back(make_md("user-agent", "grpc-c++/1.50.0"));

    // Encode.
    slice_buffer sb;
    hpack::compressor_encode_headers(&comp, &original, &sb, false);
    slice encoded = sb.merge();
    ASSERT_GT(encoded.size(), 0u);

    // Decode.
    std::vector<hpack::mdelem_data> decoded;
    int r = hpack::decode_headers(encoded.data(), static_cast<uint32_t>(encoded.size()),
                                  &dyn_table, &decoded);
    ASSERT_EQ(r, 0);
    verify_headers(decoded, original);

    hpack::compressor_destroy(&comp);
}

// ---------------------------------------------------------------------------
// Test: round-trip with multiple sequential encode/decode cycles to exercise
// dynamic table references on both the compressor and decoder sides.
// ---------------------------------------------------------------------------
TEST(HpackTest, RoundTripMultipleCycles) {
    hpack::compressor comp;
    hpack::compressor_init(&comp);
    hpack::dynamic_metadata_table dyn_table(4096);

    for (int cycle = 0; cycle < 3; cycle++) {
        std::vector<hpack::mdelem_data> original;
        original.push_back(make_md(":status", "200"));
        original.push_back(make_md("content-type", "application/grpc"));
        original.push_back(make_md("x-cycle-id", std::to_string(cycle).c_str()));

        slice_buffer sb;
        hpack::compressor_encode_headers(&comp, &original, &sb, false);
        slice encoded = sb.merge();

        std::vector<hpack::mdelem_data> decoded;
        int r = hpack::decode_headers(encoded.data(),
                                      static_cast<uint32_t>(encoded.size()), &dyn_table,
                                      &decoded);
        ASSERT_EQ(r, 0);
        verify_headers(decoded, original);
    }

    hpack::compressor_destroy(&comp);
}

// ---------------------------------------------------------------------------
// Test: encoding the same headers twice uses dynamic-table references on the
// second pass, producing shorter output.
// ---------------------------------------------------------------------------
TEST(HpackTest, EncodeRepeatedHeaders) {
    hpack::compressor comp;
    hpack::compressor_init(&comp);

    std::vector<hpack::mdelem_data> headers;
    headers.push_back(make_md(":status", "200"));
    headers.push_back(make_md("content-type", "application/grpc"));

    // First encode: full literals.
    slice_buffer sb1;
    hpack::compressor_encode_headers(&comp, &headers, &sb1, false);
    slice first = sb1.merge();

    // Second encode: should use indexed references where possible.
    slice_buffer sb2;
    hpack::compressor_encode_headers(&comp, &headers, &sb2, false);
    slice second = sb2.merge();

    // The second encoding should be shorter (indexed references are 1 byte).
    ASSERT_LT(second.size(), first.size());

    hpack::compressor_destroy(&comp);
}

// ---------------------------------------------------------------------------
// Test: dynamic table eviction under size pressure.
// ---------------------------------------------------------------------------
TEST(HpackTest, DynamicTableEviction) {
    // Use a very small table so entries get evicted quickly.
    hpack::dynamic_metadata_table small_table(64);

    ASSERT_EQ(small_table.max_table_size(), 64u);
    ASSERT_EQ(small_table.entry_count(), 0u);

    // Each entry costs 32 + key_len + value_len bytes.
    // "x-aaa: 111" -> 32 + 5 + 3 = 40 bytes  (fits)
    hpack::mdelem_data md1;
    md1.key.assign("x-aaa");
    md1.value.assign("111");
    small_table.push_mdelem_data(md1);
    ASSERT_EQ(small_table.entry_count(), 1u);

    // "x-bbb: 222" -> 40 bytes.  Total would be 80 > 64, so md1 is evicted.
    hpack::mdelem_data md2;
    md2.key.assign("x-bbb");
    md2.value.assign("222");
    small_table.push_mdelem_data(md2);
    // md1 should have been evicted to make room.
    ASSERT_EQ(small_table.entry_count(), 1u);

    // Verify the remaining entry is md2.
    hpack::mdelem_data retrieved;
    bool ok = small_table.get_mdelem_data(0, &retrieved);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(retrieved.key.compare("x-bbb"));
    ASSERT_TRUE(retrieved.value.compare("222"));
}

// ---------------------------------------------------------------------------
// Test: dynamic table size update via update_max_table_size.
// ---------------------------------------------------------------------------
TEST(HpackTest, DynamicTableSizeUpdate) {
    hpack::dynamic_metadata_table table(4096);
    ASSERT_EQ(table.max_table_size(), 4096u);

    // Reduce the max size.
    table.update_max_table_size(128);
    ASSERT_EQ(table.max_table_size(), 128u);

    // Reduce the limit (simulating SETTINGS_HEADER_TABLE_SIZE).
    table.update_max_table_size_limit(64);
    ASSERT_EQ(table.max_table_size_limit(), 64u);
}

// ---------------------------------------------------------------------------
// Test: static table lookups for well-known HTTP/2 headers.
// ---------------------------------------------------------------------------
TEST(HpackTest, StaticTableLookup) {
    // :status 200 is static entry 8 (1-based).
    hpack::mdelem_data status200 = make_md(":status", "200");
    ASSERT_EQ(full_match_static_mdelem_index(status200), 8u);

    // :method GET is static entry 2.
    hpack::mdelem_data method_get = make_md(":method", "GET");
    ASSERT_EQ(full_match_static_mdelem_index(method_get), 2u);

    // :method POST is static entry 3.
    hpack::mdelem_data method_post = make_md(":method", "POST");
    ASSERT_EQ(full_match_static_mdelem_index(method_post), 3u);

    // :path / is static entry 4.
    hpack::mdelem_data path_slash = make_md(":path", "/");
    ASSERT_EQ(full_match_static_mdelem_index(path_slash), 4u);

    // :scheme https is static entry 7.
    hpack::mdelem_data scheme_https = make_md(":scheme", "https");
    ASSERT_EQ(full_match_static_mdelem_index(scheme_https), 7u);

    // content-type key exists in the standard static table (entry 31, key-only).
    ASSERT_TRUE(check_key_exists(slice("content-type")));

    // accept-encoding key exists in the standard static table (entry 16).
    ASSERT_TRUE(check_key_exists(slice("accept-encoding")));

    // A custom key does not exist in the standard static table.
    ASSERT_TRUE(!check_key_exists(slice("x-custom-header")));

    // A header not in the static table returns 0.
    hpack::mdelem_data custom = make_md("x-custom", "value");
    ASSERT_EQ(full_match_static_mdelem_index(custom), 0u);
}

// ---------------------------------------------------------------------------
// Test: decode a response-like header block that uses only static entries.
// ---------------------------------------------------------------------------
TEST(HpackTest, DecodeStaticOnly) {
    hpack::dynamic_metadata_table dyn_table(4096);

    // Encode ":status 200" as indexed static entry 8 -> 0x88.
    uint8_t data[] = {0x88};
    std::vector<hpack::mdelem_data> headers;
    int r = hpack::decode_headers(data, 1, &dyn_table, &headers);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(headers.size(), 1u);
    ASSERT_TRUE(headers[0].key.compare(":status"));
    ASSERT_TRUE(headers[0].value.compare("200"));
}

// ---------------------------------------------------------------------------
// Test: encode gRPC-style response headers and verify round-trip.
// ---------------------------------------------------------------------------
TEST(HpackTest, EncodeGrpcResponse) {
    hpack::compressor comp;
    hpack::compressor_init(&comp);
    hpack::dynamic_metadata_table dyn_table(4096);

    // First response.
    std::vector<hpack::mdelem_data> resp;
    resp.push_back(make_md(":status", "200"));
    resp.push_back(make_md("content-type", "application/grpc"));
    resp.push_back(make_md("grpc-accept-encoding", "identity,deflate,gzip"));
    resp.push_back(make_md("accept-encoding", "identity,gzip"));

    slice_buffer sb;
    hpack::compressor_encode_headers(&comp, &resp, &sb, false);
    slice encoded = sb.merge();
    ASSERT_GT(encoded.size(), 0u);

    // Decode and verify.
    std::vector<hpack::mdelem_data> decoded;
    int r = hpack::decode_headers(encoded.data(), static_cast<uint32_t>(encoded.size()),
                                  &dyn_table, &decoded);
    ASSERT_EQ(r, 0);
    verify_headers(decoded, resp);

    // Second response: same headers -> compressor uses indexed references.
    slice_buffer sb2;
    hpack::compressor_encode_headers(&comp, &resp, &sb2, false);
    slice encoded2 = sb2.merge();

    // The second encoding should be shorter.
    ASSERT_LT(encoded2.size(), encoded.size());

    // Decode the second encoding and verify.
    std::vector<hpack::mdelem_data> decoded2;
    r = hpack::decode_headers(encoded2.data(), static_cast<uint32_t>(encoded2.size()),
                              &dyn_table, &decoded2);
    ASSERT_EQ(r, 0);
    verify_headers(decoded2, resp);

    // Trailer.
    std::vector<hpack::mdelem_data> trailer;
    trailer.push_back(make_md("grpc-status", "0"));

    slice_buffer sb3;
    hpack::compressor_encode_headers(&comp, &trailer, &sb3, false);
    slice encoded3 = sb3.merge();

    std::vector<hpack::mdelem_data> decoded3;
    r = hpack::decode_headers(encoded3.data(), static_cast<uint32_t>(encoded3.size()),
                              &dyn_table, &decoded3);
    ASSERT_EQ(r, 0);
    verify_headers(decoded3, trailer);

    // Same trailer again.  "grpc-status" is not in the standard static table
    // (check_key_exists only checks indices 1..61), so the compressor encodes
    // it without indexing each time.  Verify the round-trip still works.
    slice_buffer sb4;
    hpack::compressor_encode_headers(&comp, &trailer, &sb4, false);
    slice encoded4 = sb4.merge();

    std::vector<hpack::mdelem_data> decoded4;
    r = hpack::decode_headers(encoded4.data(), static_cast<uint32_t>(encoded4.size()),
                              &dyn_table, &decoded4);
    ASSERT_EQ(r, 0);
    verify_headers(decoded4, trailer);

    hpack::compressor_destroy(&comp);
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    init_static_metadata_context();
    int r = test::RunAllTests();
    destroy_static_metadata_context();
    return r;
}
