/**
 * @file decode.cc
 * @brief HPACK integer and string decoding implementations.
 */

#include "src/hpack/decode.h"
#include <memory>
#include "src/hpack/huffman.h"
#include "src/utils/useful.h"

namespace hpack {

const uint8_t *decode_uint32(const uint8_t *src, const uint8_t *src_end, uint32_t &dst, uint8_t mask) {
    dst = *src & mask;
    if (dst == static_cast<uint32_t>(mask)) {
        uint32_t M = 0;
        do {
            if (++src >= src_end) {
                dst = static_cast<uint32_t>(-1);
                return nullptr;
            }

            dst += (static_cast<uint32_t>(*src & 0x7f)) << M;
            M += 7;
        } while (*src & 0x80);
    }

    return ++src;
}

/**
 * @brief Decode a Huffman-encoded string into a plain string value.
 *
 * @param src     Pointer to the Huffman-encoded bytes.
 * @param src_len Number of encoded bytes to consume.
 * @param value   Output string receiving the decoded result.
 * @param len     Expected decoded length hint (used for buffer sizing).
 * @return Pointer past the consumed encoded bytes, or nullptr on decode failure.
 */
const uint8_t *decode_string(const uint8_t *src, size_t src_len, std::string &value, size_t len) {
    http2_hd_huff_decode_context ctx;
    http2_head_huffman_decode_context_init(&ctx);

    value.resize(len * 3);
    int32_t ret = http2_head_huffman_decode(&ctx, (uint8_t *)(value.data()), value.size(), src, src_len, 1);
    if (ret == -1) {
        return nullptr;
    }
    value.resize(ret);
    return src + src_len;
}

const uint8_t *parse_string(std::string &dst, const uint8_t *buf, const uint8_t *buf_end) {
    if (buf >= buf_end) return nullptr;
    uint32_t str_len = 0;

    bool huffman_decode = *buf & 0x80;

    buf = decode_uint32(buf, buf_end, str_len, INT_MASK(7));
    if (!buf) {
        return nullptr;
    }

    if (huffman_decode) {
        buf = decode_string(buf, str_len, dst, str_len);
        if (!buf) {
            return nullptr;
        }
    } else {
        if (buf + str_len <= buf_end) {
            dst = std::string(reinterpret_cast<const char *>(buf), str_len);
            buf += str_len;
        } else {
            return nullptr;  // Reading past end
        }
    }
    return buf;
}

const uint8_t *parse_string_key(std::string &dst, const uint8_t *buf, const uint8_t *buf_end) {
    if (buf >= buf_end) return nullptr;
    bool huffman_decode = *buf & 0x80;
    uint32_t str_len = 0;

    buf = decode_uint32(buf, buf_end, str_len, INT_MASK(7));
    if (!buf) {
        return nullptr;
    }

    if (huffman_decode) {
        buf = decode_string(buf, str_len, dst, str_len);
        if (!buf) {
            return nullptr;
        }
    } else {
        if (buf + str_len <= buf_end) {
            buf_end = buf + str_len;

            while (buf < buf_end) {
                char c = char(*(buf++));
                if (isupper(c)) {
                    return nullptr;
                }
                dst += c;
            }
        } else {
            return nullptr;  // Reading past end
        }
    }
    return buf;
}
}  // namespace hpack
