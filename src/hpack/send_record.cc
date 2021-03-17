#include "src/hpack/send_record.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <algorithm>

#include "src/hpack/encode.h"
#include "src/hpack/static_metadata.h"
#include "src/utils/log.h"

#define HASH_FRAGMENT_MASK (HPACK_NUM_VALUES - 1)
#define HASH_FRAGMENT_1(x) ((x)&HASH_FRAGMENT_MASK)
#define HASH_FRAGMENT_2(x) (((x) >> HPACK_NUM_VALUES_BITS) & HASH_FRAGMENT_MASK)
#define HASH_FRAGMENT_3(x) (((x) >> (HPACK_NUM_VALUES_BITS * 2)) & HASH_FRAGMENT_MASK)
#define HASH_FRAGMENT_4(x) (((x) >> (HPACK_NUM_VALUES_BITS * 3)) & HASH_FRAGMENT_MASK)

#define MDELEM_FULL_MATCH 0
#define MDELEM_KEY_MATCH 1
#define MDELEM_NOT_MATCH -1

namespace hpack {

static bool TableEmptyAt(const send_record *hashtable, uint32_t hash_index) {
    return (hashtable[hash_index].mdel == mdelem_data());
}

static bool FullMatches(const send_record *hashtable, const mdelem_data &element,
                        uint32_t hash_index) {
    return (element == hashtable[hash_index].mdel);
}

static inline void UpdateIndex(send_record *hashtable, uint32_t hash_index, uint32_t hpack_index) {
    hashtable[hash_index].index = hpack_index;
}

static void SetIndex(send_record *hashtable, uint32_t hash_index, const mdelem_data &element,
                     uint32_t hpack_index) {
    hashtable[hash_index].mdel = element;
    UpdateIndex(hashtable, hash_index, hpack_index);
}

static int GetMatchingIndex(compressor *c, const mdelem_data &mdel, uint32_t key_hash,
                            uint32_t *index) {
    const uint32_t cuckoo_first = HASH_FRAGMENT_2(key_hash);
    const uint32_t cuckoo_second = HASH_FRAGMENT_3(key_hash);

    if (TableEmptyAt(c->entries, cuckoo_first) && TableEmptyAt(c->entries, cuckoo_second)) {
        return MDELEM_NOT_MATCH;
    }

    if (FullMatches(c->entries, mdel, cuckoo_first)) {
        *index = c->entries[cuckoo_first].index;
        return MDELEM_FULL_MATCH;
    }

    if (FullMatches(c->entries, mdel, cuckoo_second)) {
        *index = c->entries[cuckoo_second].index;
        return MDELEM_FULL_MATCH;
    }

    uint32_t hpack_idx_a = c->entries[cuckoo_first].index;
    uint32_t hpack_idx_b = c->entries[cuckoo_second].index;

    uint32_t max_idx = std::max(hpack_idx_a, hpack_idx_b);
    uint32_t min_idx = std::min(hpack_idx_a, hpack_idx_b);

    if (min_idx > c->tail_remote_index) {
        *index = min_idx;
    } else if (max_idx > c->tail_remote_index) {
        *index = max_idx;
    } else {
        // never reach
        *index = c->tail_remote_index;
    }
    return MDELEM_KEY_MATCH;
}
static mdelem_data ReplaceOlderIndex(send_record *hashtable, const mdelem_data &value,
                                     uint32_t hash_index_a, uint32_t hash_index_b,
                                     uint32_t new_index) {
    const uint32_t hpack_idx_a = hashtable[hash_index_a].index;
    const uint32_t hpack_idx_b = hashtable[hash_index_b].index;
    const uint32_t id = hpack_idx_a < hpack_idx_b ? hash_index_a : hash_index_b;
    mdelem_data old = hashtable[id].mdel;
    SetIndex(hashtable, id, value, new_index);
    return old;
}

static void UpdateAddOrEvict(send_record *hashtable, const mdelem_data &value, uint32_t value_hash,
                             uint32_t new_index) {
    const uint32_t cuckoo_first = HASH_FRAGMENT_2(value_hash);
    if (FullMatches(hashtable, value, cuckoo_first)) {
        UpdateIndex(hashtable, cuckoo_first, new_index);
        return;
    }
    if (TableEmptyAt(hashtable, cuckoo_first)) {
        SetIndex(hashtable, cuckoo_first, value, new_index);
        return;
    }

    const uint32_t cuckoo_second = HASH_FRAGMENT_3(value_hash);
    if (FullMatches(hashtable, value, cuckoo_second)) {
        UpdateIndex(hashtable, cuckoo_second, new_index);
        return;
    }

    if (TableEmptyAt(hashtable, cuckoo_second)) {
        SetIndex(hashtable, cuckoo_second, value, new_index);
        return;
    }
    ReplaceOlderIndex(hashtable, value, cuckoo_first, cuckoo_second, new_index);
}

static void evict_entry(compressor *c) {
    c->tail_remote_index++;
    assert(c->tail_remote_index > 0);
    assert(c->table_size >= c->table_elem_size[c->tail_remote_index % c->cap_table_elems]);
    assert(c->table_elems > 0);
    c->table_size = static_cast<uint16_t>(
        c->table_size - c->table_elem_size[c->tail_remote_index % c->cap_table_elems]);
    c->table_elems--;
}

// Reserve space in table for the new element, evict entries if needed.
// Return the new index of the element. Return 0 to indicate not adding to
// table.
static uint32_t prepare_space_for_new_elem(compressor *c, size_t elem_size) {
    uint32_t new_index = c->tail_remote_index + c->table_elems + 1;
    assert(elem_size < 65536);

    // TODO(arjunroy): Re-examine semantics
    if (elem_size > c->max_table_size) {
        while (c->table_size > 0) {
            evict_entry(c);
        }
        return 0;
    }

    /* Reserve space for this element in the remote table: if this overflows
       the current table, drop elements until it fits, matching the decompressor
       algorithm */
    while (c->table_size + elem_size > c->max_table_size) {
        evict_entry(c);
    }
    assert(c->table_elems < c->max_table_size);
    c->table_elem_size[new_index % c->cap_table_elems] = static_cast<uint16_t>(elem_size);
    c->table_size = static_cast<uint16_t>(c->table_size + elem_size);
    c->table_elems++;

    return new_index;
}

static void add_elem(compressor *c, mdelem_data elem, size_t elem_size, uint32_t key_hash) {
    uint32_t new_index = prepare_space_for_new_elem(c, elem_size);
    if (new_index != 0) {
        UpdateAddOrEvict(c->entries, elem, key_hash, new_index);
    }
}

static uint32_t dynidx(compressor *c, uint32_t elem_index) {
    return 1 + HPACK_STATIC_MDELEM_STANDARD_COUNT + c->tail_remote_index + c->table_elems -
           elem_index;
}

static slice hpack_encode_header(compressor *c, mdelem_data mdel, bool use_true_binary_metadata) {
    const slice &key = mdel.key;
    uint32_t key_hash = mdelem_kv_hash(key);
    uint32_t indices_key = 0;

    const size_t decoder_space_usage = get_size_in_hpack_table(mdel, use_true_binary_metadata);

    int result = GetMatchingIndex(c, mdel, key_hash, &indices_key);
    if (result == MDELEM_NOT_MATCH || indices_key <= c->tail_remote_index) {
        bool key_interned = check_key_exists(key);  // static_metatdat
        if (key_interned) {
            add_elem(c, mdel, decoder_space_usage, key_hash);
            return encode_with_incremental_indexing(mdel);
        }
        return encode_without_indexing(mdel);
    }

    uint32_t idx = dynidx(c, indices_key);
    if (result == MDELEM_FULL_MATCH) {
        return encode_index(idx);
    } else {
        add_elem(c, mdel, decoder_space_usage, key_hash);
        return encode_without_indexing(mdel, idx);
    }
}

void compressor_encode_headers(compressor *c, const std::vector<mdelem_data> *metadata,
                               slice_buffer *output, bool use_true_binary_metadata) {
    if (metadata) {
        for (size_t i = 0; i < metadata->size(); i++) {
            const mdelem_data &md = metadata->at(i);
            uint32_t index = full_match_static_mdelem_index(md);
            if (index != 0) {
                slice s = encode_index(index);
                output->add_slice(s);
            } else {
                slice s = hpack_encode_header(c, md, use_true_binary_metadata);
                output->add_slice(s);
            }
        }
    }
}

static uint32_t elems_for_bytes(uint32_t bytes) {
    return (bytes + 31) / 32;
}
void compressor_init(compressor *c) {
    c->max_table_size = HPACK_INITIAL_TABLE_SIZE;
    c->cap_table_elems = elems_for_bytes(c->max_table_size);
    c->max_table_elems = c->cap_table_elems;
    c->max_usable_size = HPACK_INITIAL_TABLE_SIZE;

    c->tail_remote_index = 0;
    c->table_size = 0;
    c->table_elems = 0;

    const size_t alloc_size = sizeof(*c->table_elem_size) * c->cap_table_elems;
    c->table_elem_size = static_cast<uint16_t *>(malloc(alloc_size));
    memset(c->table_elem_size, 0, alloc_size);

    for (size_t i = 0; i < HPACK_NUM_VALUES; i++) {
        c->entries[i].index = 0;
        c->entries[i].mdel.key = slice();
        c->entries[i].mdel.value = slice();
    }
}
void compressor_destroy(compressor *c) {
    free(c->table_elem_size);
}

static void rebuild_elems(compressor *c, uint32_t new_cap) {
    uint16_t *table_elem_size = static_cast<uint16_t *>(malloc(sizeof(*table_elem_size) * new_cap));
    uint32_t i;

    memset(table_elem_size, 0, sizeof(*table_elem_size) * new_cap);
    assert(c->table_elems <= new_cap);

    for (i = 0; i < c->table_elems; i++) {
        uint32_t ofs = c->tail_remote_index + i + 1;
        table_elem_size[ofs % new_cap] = c->table_elem_size[ofs % c->cap_table_elems];
    }

    c->cap_table_elems = new_cap;
    free(c->table_elem_size);
    c->table_elem_size = table_elem_size;
}

void compressor_set_max_table_size(compressor *c, uint32_t max_table_size) {
    max_table_size = std::min(max_table_size, c->max_usable_size);
    if (max_table_size == c->max_table_size) {
        return;
    }
    while (c->table_size > 0 && c->table_size > max_table_size) {
        evict_entry(c);
    }
    c->max_table_size = max_table_size;
    c->max_table_elems = elems_for_bytes(max_table_size);
    if (c->max_table_elems > c->cap_table_elems) {
        rebuild_elems(c, std::max(c->max_table_elems, 2 * c->cap_table_elems));
    } else if (c->max_table_elems < c->cap_table_elems / 3) {
        uint32_t new_cap = std::max(c->max_table_elems, uint32_t(16));
        if (new_cap != c->cap_table_elems) {
            rebuild_elems(c, new_cap);
        }
    }
    log_debug("set max table size from encoder to %d", max_table_size);
}

void compressor_set_max_usable_size(compressor *c, uint32_t max_table_size) {
    c->max_usable_size = max_table_size;
    compressor_set_max_table_size(c, std::min(c->max_table_size, max_table_size));
}

static size_t get_base64_encoded_size(size_t raw_length) {
    static const uint8_t tail_xtra[3] = {0, 2, 3};
    return raw_length / 3 * 4 + tail_xtra[raw_length % 3];
}

inline int grpc_key_is_binary_header(const uint8_t *buf, size_t length) {
    if (length < 5) return 0;
    return 0 == memcmp(buf + length - 4, "-bin", 4);
}

size_t get_size_in_hpack_table(hpack::mdelem_data elem, bool use_true_binary_metadata) {
    const uint8_t *key_buf = elem.key.data();
    size_t key_len = elem.key.size();
    size_t overhead_and_key = 32 + key_len;
    size_t value_len = elem.value.size();
    if (grpc_key_is_binary_header(key_buf, key_len)) {
        return overhead_and_key +
               (use_true_binary_metadata ? value_len + 1 : get_base64_encoded_size(value_len));
    } else {
        return overhead_and_key + value_len;
    }
}
}  // namespace hpack
