/**
 * @file byte_order.cc
 * @brief Implementation of big-endian stream read/write functions.
 */

#include "src/utils/byte_order.h"
#include <string.h>

uint16_t get_uint16_from_be_stream(const uint8_t *p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t get_uint32_from_be_stream(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

void put_uint16_in_be_stream(uint8_t *buf, uint16_t n) {
    uint16_t x = change_byte_order(n);
    memcpy(buf, &x, sizeof(uint16_t));
}

void put_uint32_in_be_stream(uint8_t *buf, uint32_t n) {
    uint32_t x = change_byte_order(n);
    memcpy(buf, &x, sizeof(uint32_t));
}
