/**
 * @file hpack.h
 * @brief Top-level HPACK header decoding entry point.
 */

#pragma once
#include <stdint.h>
#include <vector>

#include "src/hpack/metadata.h"
#include "src/utils/slice_buffer.h"

/** @brief HPACK compression/decompression namespace (RFC 7541). */
namespace hpack {

/**
 * @brief Decode an HPACK-encoded HEADERS frame payload into a list of key-value pairs.
 *
 * Iterates through the buffer, handling indexed, literal, and dynamic table size
 * update representations per RFC 7541 Section 6.
 *
 * @param buf        Pointer to the start of the HEADERS frame payload.
 * @param buf_len    Length of the payload in bytes.
 * @param dynamic_table  Dynamic table service used for index lookups and updates.
 * @param decoded_headers  Output vector that receives the decoded metadata elements.
 * @return Http2ErrorCode cast to int (0 = NoError).
 */
int decode_headers(const uint8_t *buf, uint32_t buf_len, dynamic_table_service *dynamic_table,
                   std::vector<mdelem_data> *decoded_headers);

}  // namespace hpack
