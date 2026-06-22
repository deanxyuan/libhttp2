/**
 * @file dynamic_metadata.cc
 * @brief HPACK dynamic table implementation.
 */

#include "src/hpack/dynamic_metadata.h"
#include <stdio.h>

namespace hpack {

/**
 * @brief Construct a dynamic table with a given default maximum size.
 * @param default_max_size Initial maximum table size in bytes.
 */
dynamic_metadata_table::dynamic_metadata_table(uint32_t default_max_size)
    : _max_table_size_limit(default_max_size)
    , _max_table_size(default_max_size)
    , _current_table_size(0) {}

/** @brief Destructor. */
dynamic_metadata_table::~dynamic_metadata_table() {}

/**
 * @brief Retrieve a metadata element by index from the dynamic table.
 * @param index Zero-based index into the table.
 * @param mdel  Output pointer receiving the metadata element.
 * @return True if the index is valid, false if out of range.
 */
bool dynamic_metadata_table::get_mdelem_data(size_t index, mdelem_data *mdel) {
    auto n = _dynamic_table.size();
    if (index >= n) {
        return false;
    }

    *mdel = _dynamic_table[index];
    return true;
}

/**
 * @brief Add a new metadata element to the front of the dynamic table.
 * @param md The metadata element to insert.
 */
void dynamic_metadata_table::push_mdelem_data(const mdelem_data &md) {
    _dynamic_table.push_front(md);
    _current_table_size += MDELEM_SIZE(md);
    _current_table_size += 32;
    adjust_dynamic_table_size();
}

/**
 * @brief Update the maximum table size limit from a SETTINGS frame.
 * @param limit The new maximum table size limit in bytes.
 */
void dynamic_metadata_table::update_max_table_size_limit(uint32_t limit) {
    _max_table_size_limit = limit;
}

/**
 * @brief Update the current maximum table size from a HEADERS frame.
 * @param size The new maximum table size in bytes.
 */
void dynamic_metadata_table::update_max_table_size(uint32_t size) {
    _max_table_size = size;
    adjust_dynamic_table_size();
}

/**
 * @brief Get the current number of entries in the dynamic table.
 * @return Number of entries.
 */
size_t dynamic_metadata_table::entry_count() {
    return _dynamic_table.size();
}

/**
 * @brief Get the maximum table size limit set by SETTINGS_HEADER_TABLE_SIZE.
 * @return Maximum table size limit in bytes.
 */
uint32_t dynamic_metadata_table::max_table_size_limit() {
    return _max_table_size_limit;
}
/**
 * @brief Get the current maximum table size.
 * @return Current maximum table size in bytes.
 */
uint32_t dynamic_metadata_table::max_table_size() {
    return _max_table_size;
}

/**
 * @brief Find the index of a metadata element in the dynamic table.
 * @param mdel The metadata element to search for.
 * @return Zero-based index if found, -1 if not present.
 */
int32_t dynamic_metadata_table::get_mdelem_data_index(const mdelem_data &mdel) {
    size_t count = _dynamic_table.size();
    for (size_t i = 0; i < count; i++) {
        if (mdel.key == _dynamic_table[i].key && mdel.value == _dynamic_table[i].value) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

/** @brief Evict oldest entries until the current table size fits within the max. */
void dynamic_metadata_table::adjust_dynamic_table_size() {
    while (_current_table_size > _max_table_size && !_dynamic_table.empty()) {
        auto entry = _dynamic_table.back();
        auto element_size = MDELEM_SIZE(entry);
        _current_table_size -= element_size + 32;
        _dynamic_table.pop_back();
    }
}
}  // namespace hpack
