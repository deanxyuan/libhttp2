/**
 * @file slice_buffer.h
 * @brief Ordered collection of slices with buffer-level operations.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "src/utils/slice.h"

/**
 * @brief A buffer that manages an ordered sequence of slice objects.
 *
 * Provides operations to merge, extract, and navigate slices as a logical
 * contiguous byte stream without copying until explicitly requested.
 *
 * Uses a _start offset to make pop_front() O(1) instead of O(n).
 * Periodically compacts when the dead prefix grows too large.
 */
class slice_buffer final {
public:
    /** @brief Default constructor. Creates an empty buffer. */
    slice_buffer();

    /** @brief Destructor. */
    ~slice_buffer();

    /**
     * @brief Merges all slices into a single contiguous slice.
     * @return A new slice containing all buffered data, or an empty slice if the buffer is empty.
     */
    slice merge() const;

    /**
     * @brief Returns the number of slices currently in the buffer.
     * @return Slice count.
     */
    size_t slice_count() const;

    /**
     * @brief Returns the total byte length across all slices.
     * @return Total byte count.
     */
    size_t get_buffer_length() const;

    /**
     * @brief Appends a slice by moving it into the buffer.
     * @param s The slice to add (will be moved from).
     */
    void add_slice(slice &&s);

    /**
     * @brief Appends a slice by copying it into the buffer.
     * @param s The slice to add.
     */
    void add_slice(const slice &s);

    /**
     * @brief Extracts the first N bytes as a new contiguous slice.
     * @param len Number of bytes to extract from the front.
     * @return A new slice with the requested data, or empty if insufficient data.
     */
    slice get_header(size_t len);

    /**
     * @brief Removes the first N bytes from the buffer without returning them.
     * @param len Number of bytes to discard from the front.
     * @return True on success, false if the buffer has fewer bytes than requested.
     */
    bool move_header(size_t len);

    /** @brief Removes all slices and resets the buffer length to zero. */
    void clear_buffer();

    /**
     * @brief Checks whether the buffer contains any slices.
     * @return True if there are no slices in the buffer.
     */
    bool empty() const;

    /**
     * @brief Returns a reference to the first slice.
     * @note Asserts that the buffer is not empty.
     */
    const slice &front() const;

    /**
     * @brief Returns a reference to the last slice.
     * @note Asserts that the buffer is not empty.
     */
    const slice &back() const;

    /** @brief Removes the first slice from the buffer. */
    void pop_front();

    /** @brief Removes the last slice from the buffer. */
    void pop_back();

    /**
     * @brief Accesses a slice by index.
     * @param i Zero-based index.
     * @return Const reference to the slice at index i.
     */
    const slice &operator[](size_t i) const;

    /**
     * @brief Accesses a slice by index (alias for operator[]).
     * @param i Zero-based index.
     * @return Const reference to the slice at index i.
     */
    const slice &at(size_t i) const {
        return this->operator[](i);
    }

    /**
     * @brief Copies up to len bytes from the buffer into the destination.
     * @param buff Destination buffer pointer.
     * @param len Maximum number of bytes to copy.
     * @return Number of bytes actually copied.
     */
    size_t copy_to_buffer(void *buff, size_t len);

private:
    void _compact();

    std::vector<slice> _vs;
    size_t _start;
    size_t _length;
};
