/**
 * @file send_record.h
 * @brief HPACK compressor for encoding outgoing headers using a cuckoo-hash send table.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "src/hpack/metadata.h"
#include "src/utils/slice_buffer.h"

#define HPACK_NUM_VALUES_BITS 6                         ///< Bits used for hash table indexing.
#define HPACK_NUM_VALUES (1 << HPACK_NUM_VALUES_BITS)   ///< Number of hash table slots (64).
#define HPACK_INITIAL_TABLE_SIZE 4096                   ///< Default dynamic table size in bytes.
#define HPACK_MAX_TABLE_SIZE 1048576                    ///< Maximum allowed dynamic table size.

namespace hpack {

/** @brief A single entry in the compressor's send-record hash table. */
typedef struct {
    mdelem_data mdel;   ///< The header key-value pair.
    uint32_t index;     ///< HPACK table index for this entry.
} send_record;

/**
 * @brief HPACK compressor state for encoding outgoing headers.
 *
 * Maintains a cuckoo-hash table for fast header lookup and tracks the
 * remote decoder's dynamic table state.
 */
typedef struct {
    uint32_t max_table_size;       ///< Current maximum dynamic table size.
    uint32_t max_table_elems;      ///< Max elements that fit in max_table_size.
    uint32_t cap_table_elems;      ///< Alasured capacity of the element size array.

    uint32_t max_usable_size;      ///< Upper bound imposed by the local endpoint.

    uint32_t tail_remote_index;    ///< Index of the oldest entry still in the remote table.
    uint32_t table_size;           ///< Current estimated size of the remote dynamic table.
    uint32_t table_elems;          ///< Current number of elements tracked.
    uint16_t *table_elem_size;     ///< Circular buffer of element sizes.

    send_record entries[HPACK_NUM_VALUES];  ///< Cuckoo-hash table for send records.
} compressor;

/**
 * @brief Initialize a compressor with default settings.
 * @param c Pointer to the compressor to initialize.
 * @return 0 on success, -1 on memory allocation failure.
 */
int compressor_init(compressor *c);

/**
 * @brief Destroy a compressor and free its resources.
 * @param c Pointer to the compressor to destroy.
 */
void compressor_destroy(compressor *c);

/**
 * @brief Set the maximum dynamic table size for the compressor.
 *
 * Evicts entries if the current table exceeds the new size.
 * @param c              Pointer to the compressor.
 * @param max_table_size New maximum table size in bytes.
 */
void compressor_set_max_table_size(compressor *c, uint32_t max_table_size);

/**
 * @brief Set the maximum usable table size (local endpoint limit).
 * @param c              Pointer to the compressor.
 * @param max_table_size Maximum usable table size in bytes.
 */
void compressor_set_max_usable_size(compressor *c, uint32_t max_table_size);

/**
 * @brief Encode a vector of metadata elements into HPACK wire format.
 *
 * For each element, first checks the static table for a full match, then
 * uses the compressor's send-record table for dynamic encoding.
 * @param c                      Pointer to the compressor.
 * @param metadata               Pointer to the vector of metadata elements to encode.
 * @param output                 Output slice buffer receiving encoded bytes.
 * @param use_true_binary_metadata  If true, encode binary headers as raw bytes; otherwise base64.
 */
void compressor_encode_headers(compressor *c, const std::vector<mdelem_data> *metadata,
                               slice_buffer *output, bool use_true_binary_metadata);

/**
 * @brief Calculate the byte size a metadata element would occupy in the HPACK table.
 * @param elem                    The metadata element to measure.
 * @param use_true_binary_metadata  If true, binary headers use raw size; otherwise base64 size.
 * @return Size in bytes including the 32-byte overhead.
 */
size_t get_size_in_hpack_table(mdelem_data elem, bool use_ture_binary_metadata);
}  // namespace hpack
