#include <string.h>
#include "src/hpack/huffman.h"
#include "src/utils/testutil.h"

class HuffmanTest {};

TEST(HuffmanTest, EncodeAndDecode) {

    const char *str = "HelloWorld, This is a Test Msg.";
    size_t str_len = strlen(str);
    size_t encode_len = http2_head_huffman_encode_count((const uint8_t *)str, str_len);
    ASSERT_TRUE(encode_len > 0);

    uint8_t buff[128] = {0};
    size_t real_len = http2_head_huffman_encode(buff, sizeof(buff), (const uint8_t *)str, str_len);
    ASSERT_EQ(encode_len, real_len);

    http2_hd_huff_decode_context ctx;
    http2_head_huffman_decode_context_init(&ctx);

    uint8_t decode_buf[128] = {0};
    size_t decode_len = http2_head_huffman_decode(&ctx, decode_buf, sizeof(decode_buf), buff, real_len, 1);
    ASSERT_TRUE(strncmp(str, (char *)decode_buf, str_len) == 0);
    ASSERT_EQ(decode_len, str_len);
}

int main(int argc, char **argv) {
    return test::RunAllTests();
}
