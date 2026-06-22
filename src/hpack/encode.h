/**
 * @file encode.h
 * @brief HPACK encoding functions for header representations (RFC 7541 Section 6).
 */

#include <stdint.h>

#include "src/hpack/metadata.h"
#include "src/utils/slice.h"

namespace hpack {

/**
 * @brief Encode an integer using HPACK variable-length integer encoding (RFC 7541 Section 5.1).
 *
 * @param I    The integer value to encode.
 * @param mask N-bit prefix mask (determines the prefix width).
 * @return A slice containing the encoded bytes.
 */
slice encode_uint16(uint32_t I, uint8_t mask);

/**
 * @brief Encode an indexed header field representation (RFC 7541 Section 6.1).
 *
 * @param index The index into the static or dynamic table.
 * @return A slice containing the encoded indexed representation.
 */
slice encode_index(uint32_t index);

/**
 * @brief Encode a dynamic table size update representation (RFC 7541 Section 6.3).
 *
 * @param max_size The new maximum dynamic table size.
 * @return A slice containing the encoded size update.
 */
slice encode_update_max_size(uint32_t max_size);

/**
 * @brief Encode a literal header field with incremental indexing (RFC 7541 Section 6.2.1).
 *
 * Both key and value are encoded as literal strings; the entry is added to the
 * dynamic table by the decoder.
 *
 * @param mdel The metadata element (key-value pair) to encode.
 * @return A slice containing the encoded literal representation.
 */
slice encode_with_incremental_indexing(const mdelem_data &mdel);

/**
 * @brief Encode a literal header field without indexing, using a pre-indexed key (RFC 7541 Section 6.2.2).
 *
 * The key is referenced by index; the value is encoded as a literal string.
 * The entry is not added to the dynamic table.
 *
 * @param mdel      The metadata element whose value to encode.
 * @param key_index The table index of the header field name.
 * @return A slice containing the encoded literal representation.
 */
slice encode_without_indexing(const mdelem_data &mdel, uint32_t key_index);

/**
 * @brief Encode a literal header field without indexing, with both key and value literal (RFC 7541 Section 6.2.2).
 *
 * @param mdel The metadata element (key-value pair) to encode.
 * @return A slice containing the encoded literal representation.
 */
slice encode_without_indexing(const mdelem_data &mdel);

/**
 * @brief Encode a literal header field never indexed (RFC 7541 Section 6.2.3).
 *
 * Similar to without-indexing but signals intermediaries must not compress the value.
 *
 * @param mdel The metadata element (key-value pair) to encode.
 * @return A slice containing the encoded never-indexed representation.
 */
slice encode_never_indexed(const mdelem_data &mdel);

}  // namespace hpack
