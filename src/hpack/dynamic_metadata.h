/**
 * @file dynamic_metadata.h
 * @brief HPACK dynamic table implementation (RFC 7541 Section 4.2).
 */

#pragma once

#include <stdint.h>
#include <deque>

#include "src/hpack/metadata.h"

namespace hpack {

/**
 * @brief HPACK dynamic table storing header entries as a FIFO deque.
 *
 * Implements the dynamic_table_service interface. Entries are pushed to the
 * front and evicted from the back when the table exceeds its size limit.
 */
class dynamic_metadata_table : public dynamic_table_service {
public:
    /**
     * @brief Construct a dynamic table with a given default maximum size.
     * @param default_max_size Initial maximum table size in bytes (typically 4096).
     */
    explicit dynamic_metadata_table(uint32_t default_max_size);

    /** @brief Destructor. */
    ~dynamic_metadata_table();

    /**
     * @brief Retrieve a metadata element by its index in the dynamic table.
     * @param index Zero-based index into the table.
     * @param mdel  Output pointer receiving the metadata element.
     * @return True if the index is valid, false if out of range.
     */
    bool get_mdelem_data(size_t index, mdelem_data *mdel);

    /**
     * @brief Add a new metadata element to the front of the dynamic table.
     *
     * Evicts oldest entries if the table exceeds its maximum size after insertion.
     * @param md The metadata element to insert.
     */
    void push_mdelem_data(const mdelem_data &md);

    /**
     * @brief Find the index of a metadata element in the dynamic table.
     * @param mdel The metadata element to search for.
     * @return Zero-based index if found, -1 if not present.
     */
    int32_t get_mdelem_data_index(const mdelem_data &mdel);

    /**
     * @brief Update the maximum table size limit from a SETTINGS frame.
     *
     * Called when receiving SETTINGS_HEADER_TABLE_SIZE from the peer.
     * @param limit The new maximum table size limit in bytes.
     */
    void update_max_table_size_limit(uint32_t limit);

    /**
     * @brief Update the current maximum table size from a HEADERS frame.
     *
     * Called when processing a Dynamic Table Size Update (RFC 7541 Section 6.3).
     * @param size The new maximum table size in bytes.
     */
    void update_max_table_size(uint32_t size);

    /**
     * @brief Get the current number of entries in the dynamic table.
     * @return Number of entries.
     */
    size_t entry_count();

    /**
     * @brief Get the maximum table size limit set by SETTINGS_HEADER_TABLE_SIZE.
     * @return Maximum table size limit in bytes.
     */
    uint32_t max_table_size_limit();

    /**
     * @brief Get the current maximum table size (may be smaller than the limit).
     * @return Current maximum table size in bytes.
     */
    uint32_t max_table_size();

private:
    /** @brief Evict oldest entries from the table until it fits within the max size. */
    void adjust_dynamic_table_size();

    uint32_t _max_table_size_limit;  ///< set by SETTINGS_HEADER_TABLE_SIZE
    uint32_t _max_table_size;        ///< set by HEADER FRAME
    uint32_t _current_table_size;
    std::deque<mdelem_data> _dynamic_table;
};

}  // namespace hpack
