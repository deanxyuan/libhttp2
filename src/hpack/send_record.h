#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "src/hpack/metadata.h"
#include "src/utils/slice_buffer.h"

#define HPACK_NUM_VALUES_BITS 6
#define HPACK_NUM_VALUES (1 << HPACK_NUM_VALUES_BITS)
#define HPACK_INITIAL_TABLE_SIZE 4096
#define HPACK_MAX_TABLE_SIZE 1048576

namespace hpack {
typedef struct {
    mdelem_data mdel;
    uint32_t index;
} send_record;

typedef struct {
    uint32_t max_table_size;
    uint32_t max_table_elems;
    uint32_t cap_table_elems;

    uint32_t max_usable_size;

    uint32_t tail_remote_index;
    uint32_t table_size;
    uint32_t table_elems;
    uint16_t *table_elem_size;

    send_record entries[HPACK_NUM_VALUES];
} compressor;

void compressor_init(compressor *c);
void compressor_destroy(compressor *c);
void compressor_set_max_table_size(compressor *c, uint32_t max_table_size);
void compressor_set_max_usable_size(compressor *c, uint32_t max_table_size);
void compressor_encode_headers(compressor *c, const std::vector<mdelem_data> *metadata,
                               slice_buffer *output, bool use_true_binary_metadata);
size_t get_size_in_hpack_table(mdelem_data elem, bool use_ture_binary_metadata);
}  // namespace hpack
