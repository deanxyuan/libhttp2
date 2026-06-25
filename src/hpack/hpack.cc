/**
 * @file hpack.cc
 * @brief Implementation of the top-level HPACK header decoding logic.
 */

#include "src/hpack/hpack.h"
#include <assert.h>
#include "src/hpack/static_metadata.h"
#include "src/hpack/decode.h"
#include "src/http2/errors.h"
#include "src/utils/useful.h"
#include "src/hpack/encode.h"

/*
inline bool is_valid_header(const std::string &k, const std::string &v) {
    return k != std::string("connection") && (k != std::string("te") || v == std::string("trailers"));
}

bool string_starts_with(const std::string &str, char c) {
    if (str.empty()) return false;
    return (str[0] == c);
}
*/

// -------------------------------------------
namespace hpack {

int decode_headers(const uint8_t *buf, uint32_t buf_len, dynamic_table_service *dynamic_table,
                   std::vector<mdelem_data> *decoded_headers) {
    static_metadata *hpack_static_headers = get_static_mdelem_table();
    const uint8_t *buf_end = buf + buf_len;
    while (buf < buf_end) {
        uint32_t int_value = 0;
        if (*buf & 0x80) {
            /*  6.1 Indexed Header Field Representation

                  0   1   2   3   4   5   6   7
                +---+---+---+---+---+---+---+---+
                | 1 |        Index (7+)         |
                +---+---------------------------+
             */
            buf = decode_uint32(buf, buf_end, int_value, INT_MASK(7));

            if (!buf || int_value == 0) {
                return static_cast<int>(Http2ErrorCode::CompressionError);
            }

            mdelem_data mdel;
            // Table 1: Static Table Entries (max entries 61)
            if (int_value > HPACK_STATIC_MDELEM_STANDARD_COUNT) {
                int_value -= HPACK_STATIC_MDELEM_STANDARD_COUNT + 1;
                if (!dynamic_table->get_mdelem_data(int_value, &mdel)) {
                    return static_cast<int>(Http2ErrorCode::CompressionError);
                }

            } else {
                const auto sm = hpack_static_headers[int_value];
                mdel = sm.data();
            }

            decoded_headers->push_back(mdel);

        } else {
            bool add_to_dynamic_table = false;
            if (*buf & 0x40) {
                // 6.2.1 Literal Header Field with Incremental Indexing
                /*
                      0   1   2   3   4   5   6   7
                    +---+---+---+---+---+---+---+---+
                    | 0 | 1 |      Index (6+)       |
                    +---+---+-----------------------+
                    | H |     Value Length (7+)     |
                    +---+---------------------------+
                    | Value String (Length octets)  |
                    +-------------------------------+
                 */
                buf = decode_uint32(buf, buf_end, int_value, INT_MASK(6));
                if (!buf) {
                    return static_cast<int>(Http2ErrorCode::CompressionError);
                }
                add_to_dynamic_table = true;
            } else if (*buf & 0x20) {
                // 6.3.  Dynamic Table Size Update
                /*
                    Figure: Maximum Dynamic Table Size Change

                      0   1   2   3   4   5   6   7
                    +---+---+---+---+---+---+---+---+
                    | 0 | 0 | 1 |   Max size (5+)   |
                    +---+---------------------------+
                 */
                buf = decode_uint32(buf, buf_end, int_value, INT_MASK(5));

                // The new maximum size MUST be lower than or equal to the limit
                // determined by the protocol using HPACK.
                if (!buf || int_value > dynamic_table->max_table_size_limit()) {
                    return static_cast<int>(Http2ErrorCode::CompressionError);
                }
                dynamic_table->update_max_table_size(int_value);
                continue;

            } else {
                // 6.2.2 Literal Header Field without Indexing
                // 6.2.3 Literal Header Field Never Indexed
                /*
                      0   1   2   3   4   5   6   7
                    +---+---+---+---+---+---+---+---+
                    | 0 | 0 | 0 | 1 |  Index (4+)   |
                    +---+---+-----------------------+
                    | H |     Value Length (7+)     |
                    +---+---------------------------+
                    | Value String (Length octets)  |
                    +-------------------------------+
                 */
                buf = decode_uint32(buf, buf_end, int_value, INT_MASK(4));
                if (!buf) {
                    return static_cast<int>(Http2ErrorCode::CompressionError);
                }
            }

            mdelem_data mdel;

            if (int_value > HPACK_STATIC_MDELEM_STANDARD_COUNT) {
                // if (add_to_dynamic_table) {
                // 6.2.1 Literal Header Field with Incremental Indexing
                // Indexed Name
                int_value -= HPACK_STATIC_MDELEM_STANDARD_COUNT + 1;
                if (!dynamic_table->get_mdelem_data(int_value, &mdel)) {
                    return static_cast<int>(Http2ErrorCode::CompressionError);
                }
                //} else {
                // return static_cast<int>(Http2ErrorCode::CompressionError);
                //}
            } else if (int_value != 0) {
                const auto sm = hpack_static_headers[int_value];
                mdel.key = sm.data().key;
            } else {
                std::string key;
                buf = parse_string_key(key, buf, buf_end);
                if (!buf) {
                    return static_cast<int>(Http2ErrorCode::ProtocolError);
                }
                mdel.key.assign(key);
            }

            std::string value;
            buf = parse_string(value, buf, buf_end);
            if (!buf) {
                return static_cast<int>(Http2ErrorCode::CompressionError);
            }

            mdel.value.assign(value);

            decoded_headers->push_back(mdel);

            if (add_to_dynamic_table) {
                dynamic_table->push_mdelem_data(mdel);
            }
        }
    }

    return static_cast<int>(Http2ErrorCode::NoError);
}
}  // namespace hpack
