/**
 * @file static_metadata.h
 * @brief HPACK static table (RFC 7541 Appendix A) and related lookup functions.
 */

#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "src/hpack/metadata.h"

#define HPACK_STATIC_MDELEM_STANDARD_COUNT 61  ///< Standard HPACK static table entries (RFC 7541 Appendix A, 1-61).

namespace hpack {

/**
 * @brief A single entry in the HPACK static metadata table.
 *
 * Stores a key-value pair along with its precomputed hash and table index.
 */
class static_metadata {
public:
    /**
     * @brief Construct a static metadata entry.
     * @param key  The header field name.
     * @param value The header field value (may be nullptr for name-only entries).
     * @param idx  The 1-based index in the static table.
     */
    static_metadata(const slice &key, const slice &value, uint32_t idx);

    /**
     * @brief Get the key-value data for this entry.
     * @return Reference to the mdelem_data.
     */
    const mdelem_data &data() const;

    /**
     * @brief Get the precomputed hash of this entry.
     * @return Hash value.
     */
    uint32_t hash() const;

    /**
     * @brief Get the 1-based index of this entry in the static table.
     * @return Table index.
     */
    uint32_t index() const;

private:
    mdelem_data _kv;
    uint32_t _index;
    uint32_t _hash;
};

extern static_metadata *g_static_mdelem_table;  ///< Global pointer to the static table array.
}  // namespace hpack

/**
 * @brief Initialize the global static metadata context.
 *
 * Must be called once before any HPACK operations. Allocates the static table.
 */
void init_static_metadata_context(void);

/** @brief Destroy the global static metadata context and free resources. */
void destroy_static_metadata_context(void);

/**
 * @brief Get the global static metadata table pointer.
 * @return Pointer to the static_metadata array (asserts non-null).
 */
inline hpack::static_metadata *get_static_mdelem_table() {
    assert(hpack::g_static_mdelem_table != nullptr);
    return hpack::g_static_mdelem_table;
}

/**
 * @brief Find the exact matching entry index in the standard HPACK static table.
 * @param mdel The metadata element to match.
 * @return 1-based index if found, 0 if not found.
 */
uint32_t full_match_static_mdelem_index(const hpack::mdelem_data &mdel);

/**
 * @brief Check whether a header field name exists in the standard HPACK static table.
 * @param key The header field name to search for.
 * @return True if the key exists in the static table.
 */
bool check_key_exists(const slice &key);
