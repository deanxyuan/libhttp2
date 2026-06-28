/**
 * @file encode.cc
 * @brief HPACK encoding implementations for integer and header representations.
 */

#include "src/hpack/encode.h"
#include <string.h>
#include "src/utils/useful.h"

namespace hpack {

void uint16_encode_impl(uint8_t *buf, uint32_t I, uint8_t mask) {
    if (I < mask) {
        *buf = static_cast<uint8_t>(I);
        return;
    }

    I -= mask;
    *buf++ = mask;
    while (I >= 128) {
        *buf++ = static_cast<uint8_t>((I & 0x7f) | 0x80);
        I = I >> 7;
    }
    *buf++ = static_cast<uint8_t>(I);
}

size_t uint16_encode_length(uint32_t I, uint8_t mask) {
    if (I < mask) {
        return 1;
    }

    I -= mask;
    size_t count = 1;

    while (I >= 128) {
        I = I >> 7;
        count++;
    }
    count++;
    return count;
}

slice encode_uint16(uint32_t I, uint8_t mask) {
    size_t bytes = uint16_encode_length(I, mask);
    slice s = MakeSliceByLength(bytes);
    uint8_t *buf = s.mutable_data();
    uint16_encode_impl(buf, I, mask);
    return s;
}

slice encode_mdelem_data_impl(const mdelem_data &mdel, uint8_t type) {
    uint32_t key_size = static_cast<uint32_t>(mdel.key.size());
    uint32_t value_size = static_cast<uint32_t>(mdel.value.size());
    size_t key_len_size = uint16_encode_length(key_size, INT_MASK(7));
    size_t value_len_size = uint16_encode_length(value_size, INT_MASK(7));
    size_t bytes = 1 + key_len_size + value_len_size + MDELEM_SIZE(mdel);
    slice s = MakeSliceByLength(bytes);
    uint8_t *buf = s.mutable_data();

    // first byte (type)
    *buf++ = type;

    // key length
    uint16_encode_impl(buf, key_size, INT_MASK(7));
    buf += key_len_size;

    // key
    if (key_size > 0) {
        memcpy(buf, mdel.key.data(), key_size);
    }
    buf += key_size;

    // value length
    uint16_encode_impl(buf, value_size, INT_MASK(7));
    buf += value_len_size;

    // value
    if (value_size > 0) {
        memcpy(buf, mdel.value.data(), value_size);
    }
    return s;
}

slice encode_index(uint32_t index) {
    /*
          0   1   2   3   4   5   6   7
        +---+---+---+---+---+---+---+---+
        | 1 |        Index (7+)         |
        +---+---------------------------+
     */

    slice s = encode_uint16(index, INT_MASK(7));
    uint8_t *buf = s.mutable_data();
    buf[0] |= 0x80;
    return s;
}

slice encode_update_max_size(uint32_t max_size) {
    /*
          0   1   2   3   4   5   6   7
        +---+---+---+---+---+---+---+---+
        | 0 | 0 | 1 |   Max size (5+)   |
        +---+---------------------------+
     */

    slice s = encode_uint16(max_size, INT_MASK(5));
    uint8_t *buf = s.mutable_data();
    buf[0] |= 0x20;
    return s;
}

slice encode_with_incremental_indexing(const mdelem_data &mdel) {
    /*
          0   1   2   3   4   5   6   7
        +---+---+---+---+---+---+---+---+
        | 0 | 1 |           0           |
        +---+---+-----------------------+
        | H |     Name Length (7+)      |
        +---+---------------------------+
        |  Name String (Length octets)  |
        +---+---------------------------+
        | H |     Value Length (7+)     |
        +---+---------------------------+
        | Value String (Length octets)  |
        +-------------------------------+
     */

    return encode_mdelem_data_impl(mdel, 0x40);
}

slice encode_without_indexing(const mdelem_data &mdel, uint32_t key_index) {
    /*
          0   1   2   3   4   5   6   7
        +---+---+---+---+---+---+---+---+
        | 0 | 0 | 0 | 0 |  Index (4+)   |
        +---+---+-----------------------+
        | H |     Value Length (7+)     |
        +---+---------------------------+
        | Value String (Length octets)  |
        +-------------------------------+
     */
    uint32_t value_size = static_cast<uint32_t>(mdel.value.size());
    size_t key_index_size = uint16_encode_length(key_index, INT_MASK(4));
    size_t value_len_size = uint16_encode_length(value_size, INT_MASK(7));
    size_t bytes = key_index_size + value_len_size + value_size;
    slice s = MakeSliceByLength(bytes);
    uint8_t *buf = s.mutable_data();

    // key index
    uint16_encode_impl(buf, key_index, INT_MASK(4));
    buf += key_index_size;

    // value length
    uint16_encode_impl(buf, value_size, INT_MASK(7));
    buf += value_len_size;

    // value
    if (value_size > 0) {
        memcpy(buf, mdel.value.data(), value_size);
    }
    return s;
}

slice encode_without_indexing(const mdelem_data &mdel) {
    /*
          0   1   2   3   4   5   6   7
        +---+---+---+---+---+---+---+---+
        | 0 | 0 | 0 | 0 |       0       |
        +---+---+-----------------------+
        | H |     Name Length (7+)      |
        +---+---------------------------+
        |  Name String (Length octets)  |
        +---+---------------------------+
        | H |     Value Length (7+)     |
        +---+---------------------------+
        | Value String (Length octets)  |
        +-------------------------------+
     */

    return encode_mdelem_data_impl(mdel, 0x0);
}

slice encode_never_indexed(const mdelem_data &mdel) {
    /*
          0   1   2   3   4   5   6   7
        +---+---+---+---+---+---+---+---+
        | 0 | 0 | 0 | 1 |       0       |
        +---+---+-----------------------+
        | H |     Name Length (7+)      |
        +---+---------------------------+
        |  Name String (Length octets)  |
        +---+---------------------------+
        | H |     Value Length (7+)     |
        +---+---------------------------+
        | Value String (Length octets)  |
        +-------------------------------+
     */

    return encode_mdelem_data_impl(mdel, 0x10);
}
}  // namespace hpack
