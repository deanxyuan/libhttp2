/**
 * @file byte_order.h
 * @brief Byte-order conversion utilities for network protocol parsing.
 */

#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Reverses the byte order of an integer value (endian swap).
 * @tparam T Integer type (uint16_t, uint32_t, uint64_t, or signed equivalents).
 * @param value The value whose bytes are to be reversed.
 * @return The value with its byte order reversed.
 */
template <typename T>
T change_byte_order(T value) {
    uint8_t buf[sizeof(T)] = {0};
    uint8_t *ptr = reinterpret_cast<uint8_t *>(&value);
    for (size_t i = 0; i < sizeof(T); i++) {
        buf[i] = ptr[sizeof(T) - i - 1];
    }
    return *reinterpret_cast<T *>(buf);
}

/**
 * @brief Reads a big-endian uint16_t from a byte stream.
 * @param p Pointer to at least 2 bytes of big-endian data.
 * @return The host-byte-order uint16_t value.
 */
uint16_t get_uint16_from_be_stream(const uint8_t *p);

/**
 * @brief Reads a big-endian uint32_t from a byte stream.
 * @param p Pointer to at least 4 bytes of big-endian data.
 * @return The host-byte-order uint32_t value.
 */
uint32_t get_uint32_from_be_stream(const uint8_t *p);

/**
 * @brief Writes a uint16_t into a byte buffer in big-endian order.
 * @param buf Destination buffer (must be at least 2 bytes).
 * @param n The host-byte-order value to write.
 */
void put_uint16_in_be_stream(uint8_t *buf, uint16_t n);

/**
 * @brief Writes a uint32_t into a byte buffer in big-endian order.
 * @param buf Destination buffer (must be at least 4 bytes).
 * @param n The host-byte-order value to write.
 */
void put_uint32_in_be_stream(uint8_t *buf, uint32_t n);
