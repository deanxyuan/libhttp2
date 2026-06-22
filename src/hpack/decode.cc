/**
 * @file decode.cc
 * @brief HPACK integer and string decoding implementations.
 */

#include "src/hpack/decode.h"
#include <memory>
#include "src/hpack/huffman.h"
#include "src/utils/useful.h"

namespace hpack {

/**
 * @brief Decode a variable-length integer with N-bit prefix mask (RFC 7541 Section 5.1).
 *
 * Returns nullptr if the encoded value extends past src_end. The decoded value
 * may overflow uint32_t if the encoding is malicious; callers should validate
 * the result in context.
 *
 * @param src     Pointer to the first byte of the encoded integer.
 * @param src_end Pointer to one past the last readable byte.
 * @param dst     Output parameter receiving the decoded integer.
 * @param mask    N-bit prefix mask (e.g. 0x7f for 7-bit, 0x0f for 4-bit).
 * @return Pointer to the byte after the decoded integer, or nullptr on error.
 */
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
    int32_t ret = http2_head_huffman_decode(&ctx, (uint8_t *)(value.data()), src, src_len, 1);
    if (ret == -1) {
        return nullptr;
    }
    value.resize(ret);
    return src + src_len;
}

/**
 * @brief Parse an HPACK string literal (Huffman or raw) from the buffer.
 *
 * Reads the length prefix, checks the Huffman flag (bit 7 of the first byte),
 * and decodes the string accordingly.
 *
 * @param dst     Output string receiving the parsed value.
 * @param buf     Pointer to the first byte of the string encoding.
 * @param buf_end Pointer to one past the last readable byte.
 * @return Pointer past the consumed string bytes, or nullptr on error.
 */
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

/**
 * @brief Parse an HPACK header name string, rejecting uppercase characters.
 *
 * Like parse_string, but for header field names. Non-Huffman-encoded names
 * containing uppercase ASCII characters are rejected (returns nullptr) per
 * RFC 7541 Section 5.2.
 *
 * @param dst     Output string receiving the parsed header name.
 * @param buf     Pointer to the first byte of the string encoding.
 * @param buf_end Pointer to one past the last readable byte.
 * @return Pointer past the consumed string bytes, or nullptr on error.
 */
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
