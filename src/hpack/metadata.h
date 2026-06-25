#pragma once

#include <stddef.h>
#include <stdint.h>
#include "src/utils/slice.h"

namespace hpack {

/**
 * @brief A key-value pair representing an HTTP/2 metadata element (header).
 *
 * Used throughout the HPACK encoder/decoder to represent header entries
 * before they are encoded into the wire format or after they are decoded.
 */
typedef struct {
    slice key;   /**< Header name (e.g. ":method", "content-type") */
    slice value; /**< Header value (e.g. "GET", "application/json") */
} mdelem_data;

/**
 * @brief Interface for managing the HPACK dynamic table.
 *
 * The dynamic table (RFC 7541, Section 2.3.2) stores recently seen headers
 * for index-based encoding. Implementations must be thread-safe if the
 * transport layer is used concurrently.
 */
class dynamic_table_service {
public:
    virtual ~dynamic_table_service() {}

    /**
     * @brief Retrieve a dynamic table entry by index.
     * @param index 1-based index into the dynamic table (after static table).
     * @param mdel Output parameter; filled with the entry's key and value.
     * @return true if the index is valid and mdel was filled, false otherwise.
     */
    virtual bool get_mdelem_data(size_t index, mdelem_data *mdel) = 0;

    /**
     * @brief Append a new entry to the dynamic table.
     * @param md The metadata element to add. Evicts oldest entries if needed.
     */
    virtual void push_mdelem_data(const mdelem_data &md) = 0;

    /**
     * @brief Look up the dynamic table index for a given metadata element.
     * @param mdel The metadata element to search for.
     * @return 1-based dynamic table index, or -1 if not found.
     */
    virtual int32_t get_mdelem_data_index(const mdelem_data &mdel) = 0;

    /**
     * @brief Update the maximum dynamic table size limit.
     *
     * Called when a SETTINGS frame is received with SETTINGS_HEADER_TABLE_SIZE
     * (RFC 7540, Section 6.5.2). This sets the upper bound for subsequent
     * dynamic table size changes.
     *
     * @param limit New maximum table size limit in octets.
     */
    virtual void update_max_table_size_limit(uint32_t limit) = 0;

    /**
     * @brief Update the current dynamic table size.
     *
     * Called when a HEADERS frame with dynamic table size update is received
     * (RFC 7540, Section 6.3). Must not exceed the current limit.
     *
     * @param size New dynamic table size in octets.
     */
    virtual void update_max_table_size(uint32_t size) = 0;

    /**
     * @brief Return the number of entries currently in the dynamic table.
     * @return Entry count.
     */
    virtual size_t entry_count() = 0;

    /**
     * @brief Return the maximum allowed dynamic table size limit.
     * @return Size limit in octets (from SETTINGS_HEADER_TABLE_SIZE).
     */
    virtual uint32_t max_table_size_limit() = 0;

    /**
     * @brief Return the current dynamic table size.
     * @return Current size in octets.
     */
    virtual uint32_t max_table_size() = 0;
};

/**
 * @brief Interface for tracking which headers have been sent to the peer.
 *
 * The encoder must record which metadata elements have been sent so it can
 * decide whether to encode with incremental indexing (first time) or by
 * index reference (already known to the peer). See RFC 7541, Section 2.3.
 */
class send_record_service {
public:
    virtual ~send_record_service() {}

    /**
     * @brief Retrieve a send record by index.
     * @param index Record table index.
     * @param mdel Output parameter; filled with the recorded metadata element.
     * @return true if the index is valid, false otherwise.
     */
    virtual bool get_record(size_t index, mdelem_data *mdel) = 0;

    /**
     * @brief Append a new send record.
     * @param md The metadata element that was sent.
     */
    virtual void push_record(const mdelem_data &md) = 0;

    /**
     * @brief Look up the record index for a given metadata element.
     * @param mdel The metadata element to search for.
     * @return Record index, or -1 if not found.
     */
    virtual int32_t get_record_index(const mdelem_data &mdel) = 0;

    /**
     * @brief Check whether a metadata element has already been recorded.
     * @param md The metadata element to check.
     * @return true if the element was previously sent, false otherwise.
     */
    virtual bool check_record_exists(const mdelem_data &md) = 0;

    /**
     * @brief Return the total number of send records.
     * @return Record count.
     */
    virtual uint32_t record_count() = 0;
};

}  // namespace hpack

/**
 * @brief Compute the HPACK size of a metadata element (key + value octets).
 * @param mdel The metadata element.
 * @return Total size in octets (RFC 7541, Section 4.1).
 */
#define MDELEM_SIZE(mdel) ((mdel).key.size() + (mdel).value.size())

/**
 * @brief Compare two metadata elements for equality.
 * @param md1 First element.
 * @param md2 Second element.
 * @return true if both key and value are equal.
 */
static inline bool operator==(const hpack::mdelem_data &md1, const hpack::mdelem_data &md2) {
    return (md1.key == md2.key) && (md1.value == md2.value);
}

/**
 * @brief Compute a combined hash for a metadata element (key + value).
 * @param oth The metadata element to hash.
 * @return Combined hash value.
 */
uint32_t mdelem_data_hash(const hpack::mdelem_data &oth);

/**
 * @brief Compute a hash for a raw slice (used as key or value independently).
 * @param kv The slice to hash.
 * @return Hash value.
 */
uint32_t mdelem_kv_hash(const slice &kv);
