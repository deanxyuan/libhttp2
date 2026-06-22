/**
 * @file decode.h
 * @brief HPACK integer and string decoding primitives (RFC 7541 Section 5).
 */

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace hpack {

/**
 * @brief Decode an HPACK variable-length integer (RFC 7541 Section 5.1).
 *
 * @param src     Pointer to the first byte of the encoded integer.
 * @param src_end Pointer to one past the last readable byte.
 * @param dst     Output parameter that receives the decoded value.
 * @param mask    Bitmask applied to the first byte (N-bit prefix mask).
 * @return Pointer to the byte after the decoded integer, or nullptr on error.
 */
const uint8_t *decode_uint32(const uint8_t *src, const uint8_t *src_end, uint32_t &dst, uint8_t mask);

/**
 * @brief Decode an HPACK string literal, handling Huffman encoding if flagged.
 *
 * @param dst     Output string that receives the decoded value.
 * @param buf     Pointer to the start of the string encoding (after length prefix).
 * @param buf_end Pointer to one past the last readable byte.
 * @return Pointer past the consumed string bytes, or nullptr on error.
 */
const uint8_t *parse_string(std::string &dst, const uint8_t *buf, const uint8_t *buf_end);

/**
 * @brief Decode an HPACK header name (key) string.
 *
 * Same as parse_string but rejects uppercase characters in non-Huffman-encoded
 * header names, per RFC 7541 Section 5.2 requirement that header field names
 * must be lowercase.
 *
 * @param dst     Output string that receives the decoded key.
 * @param buf     Pointer to the start of the string encoding.
 * @param buf_end Pointer to one past the last readable byte.
 * @return Pointer past the consumed string bytes, or nullptr on error.
 */
const uint8_t *parse_string_key(std::string &dst, const uint8_t *buf, const uint8_t *buf_end);

}  // namespace hpack
