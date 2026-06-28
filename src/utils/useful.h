/**
 * @file useful.h
 * @brief Common bitwise utility macros.
 */

#pragma once

/**
 * @brief Generates a bitmask with the specified number of low bits set.
 * @param bits Number of bits to set (must be <= 31).
 * @return An integer with the lowest `bits` bits set to 1.
 */
#define INT_MASK(bits) ((1u << (bits)) - 1)

/**
 * @brief Rotates the bits of x left by n positions.
 * @param x The value to rotate.
 * @param n Number of bit positions to rotate left.
 * @return The rotated value.
 */
#define ROTL(x, n) ((((x) << (n)) | ((x) >> (sizeof(x) * 8 - (n)))))

/**
 * @brief Rotates the bits of x right by n positions.
 * @param x The value to rotate.
 * @param n Number of bit positions to rotate right.
 * @return The rotated value.
 */
#define ROTR(x, n) ((((x) >> (n)) | ((x) << (sizeof(x) * 8 - (n)))))
