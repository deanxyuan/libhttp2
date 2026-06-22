/**
 * @file slice.h
 * @brief Zero-copy byte buffer with small-buffer optimization and reference counting.
 */

#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <string>

class slice_refcount;

/**
 * @brief A reference-counted byte buffer with inline storage optimization.
 *
 * Small buffers (<= 23 bytes) are stored inline without heap allocation.
 * Larger buffers use a reference-counted heap allocation with a shared
 * control block (slice_refcount) placed immediately before the data.
 */
class slice final {
public:
    /** @brief Default constructor. Creates an empty slice. */
    slice();

    /**
     * @brief Construct a slice from a null-terminated C string.
     * @param ptr Null-terminated string to copy from.
     */
    slice(const char *ptr);

    /**
     * @brief Construct a slice from a std::string.
     * @param str Source string whose contents are copied.
     */
    slice(const std::string &str);

    /**
     * @brief Construct a slice from a const void pointer and length.
     * @param ptr Pointer to source data.
     * @param length Number of bytes to copy.
     */
    slice(const void *ptr, size_t length);

    /**
     * @brief Construct a slice from a mutable void pointer and length.
     * @param ptr Pointer to source data.
     * @param length Number of bytes to copy.
     */
    slice(void *ptr, size_t length);

    /**
     * @brief Construct a slice from a char pointer and length.
     * @param ptr Pointer to source data.
     * @param length Number of bytes to copy.
     */
    slice(const char *ptr, size_t length);

    /** @brief Destructor. Releases the reference-counted buffer if applicable. */
    ~slice();

    /**
     * @brief Copy constructor. Increments the reference count for heap-allocated data.
     * @param oth The source slice to copy.
     */
    slice(const slice &oth);

    /**
     * @brief Copy assignment operator. Releases current data and copies from source.
     * @param oth The source slice to copy.
     * @return Reference to this slice.
     */
    slice &operator=(const slice &oth);

    /**
     * @brief Move constructor. Transfers ownership without copying.
     * @param oth The source slice to move from (left in a valid empty state).
     */
    slice(slice &&oth) noexcept;

    /**
     * @brief Move assignment operator. Transfers ownership without copying.
     * @param oth The source slice to move from (left in a valid empty state).
     * @return Reference to this slice.
     */
    slice &operator=(slice &&oth) noexcept;

    /**
     * @brief Returns the number of bytes in the slice.
     * @return Size in bytes.
     */
    size_t size() const;

    /**
     * @brief Returns a pointer to the underlying byte data.
     * @return Const pointer to the byte array.
     */
    const uint8_t *data() const;

    /**
     * @brief Removes the last N bytes from the slice.
     * @param remove_size Number of bytes to remove; clamped to current size.
     */
    void pop_back(size_t remove_size);

    /**
     * @brief Removes the first N bytes from the slice.
     * @param remove_size Number of bytes to remove; clamped to current size.
     */
    void pop_front(size_t remove_size);

    /**
     * @brief Converts the slice contents to a std::string.
     * @return A string copy of the slice data.
     */
    std::string to_string() const;

    /**
     * @brief Checks whether the slice contains zero bytes.
     * @return True if the slice is empty.
     */
    bool empty() const;

    /**
     * @brief Replaces the slice contents with data from a std::string.
     * @param s The source string to copy from.
     */
    void assign(const std::string &s);

    /**
     * @brief Compares the slice contents with a std::string.
     * @param s The string to compare against.
     * @return True if both have identical content.
     */
    bool compare(const std::string &s) const;

    /**
     * @brief Equality comparison operator.
     * @param oth The other slice to compare with.
     * @return True if both slices have identical content.
     */
    bool operator==(const slice &oth) const;

    /**
     * @brief Inequality comparison operator.
     * @param oth The other slice to compare with.
     * @return True if the slices differ.
     */
    bool operator!=(const slice &oth) const {
        return !(this->operator==(oth));
    }

    /**
     * @brief Appends another slice's content to this slice.
     * @param s The slice to append.
     * @return Reference to this slice after concatenation.
     */
    slice &operator+=(const slice &s);

private:
    enum { SLICE_INLINED_SIZE = 23 };

    slice_refcount *_refs;
    union slice_data {
        struct {
            size_t length;
            uint8_t *bytes;
        } refcounted;
        struct {
            uint8_t length;
            uint8_t bytes[23];
        } inlined;
    } _data;

    friend slice MakeStaticSlice(const void *ptr, size_t len);
    friend slice MakeSliceByLength(size_t len);
    friend slice operator+(slice s1, slice s2);
};

/**
 * @brief Creates a static (non-owning) slice that stores the pointer directly without copying.
 * @param ptr Pointer to externally managed data (must outlive the slice).
 * @param len Length of the data in bytes.
 * @return A slice referencing the given pointer.
 */
slice MakeStaticSlice(const void *ptr, size_t len);

/**
 * @brief Creates a static (non-owning) slice from a null-terminated C string.
 * @param ptr Null-terminated string (must outlive the slice).
 * @return A slice referencing the given string.
 */
slice MakeStaticSlice(const char *ptr);

/**
 * @brief Creates an uninitialized slice of the specified length.
 * @param len Desired length in bytes.
 * @return A slice with the requested capacity (data is uninitialized).
 */
slice MakeSliceByLength(size_t len);

/**
 * @brief Concatenates two slices into a new slice.
 * @param s1 The first slice (left-hand side).
 * @param s2 The second slice (right-hand side).
 * @return A new slice containing the concatenation of s1 and s2.
 */
slice operator+(slice s1, slice s2);
