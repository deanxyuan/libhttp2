/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2013 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file huffman.h
 * @brief HTTP/2 HPACK Huffman coding API (encoding, decoding, and lookup tables).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    /* FSA accepts this state as the end of huffman encoding
       sequence. */
    HTTP2_HUFF_ACCEPTED = 1 << 14,
    /* This state emits symbol */
    HTTP2_HUFF_SYM = 1 << 15,
} http2_huff_decode_flag;

typedef struct {
    /* fstate is the current huffman decoding state, which is actually
       the node ID of internal huffman tree with
       http2_huff_decode_flag OR-ed.  We have 257 leaf nodes, but they
       are identical to root node other than emitting a symbol, so we
       have 256 internal nodes [1..255], inclusive.  The node ID 256 is
       a special node and it is a terminal state that means decoding
       failed. */
    uint16_t fstate;
    /* symbol if HTTP2_HUFF_SYM flag set */
    uint8_t sym;
} http2_huff_decode;

typedef struct {
    /* fstate is the current huffman decoding state. */
    uint16_t fstate;
} http2_hd_huff_decode_context;

typedef struct {
    /* The number of bits in this code */
    uint32_t nbits;
    /* Huffman code aligned to LSB */
    uint32_t code;
} http2_huff_sym;

extern const http2_huff_sym huff_sym_table[];
extern const http2_huff_decode huff_decode_table[][16];

/* Huffman encoding/decoding functions */

/**
 * @brief Count the number of bytes required to Huffman-encode the given data.
 *
 * This includes padding of the prefix of the terminal symbol code.
 * This function always succeeds.
 *
 * @param src Pointer to the source data.
 * @param len Length of the source data in bytes.
 * @return Number of bytes required for the encoded output.
 */
size_t http2_head_huffman_encode_count(const uint8_t *src, size_t len);

/**
 * @brief Huffman-encode the source data into the destination buffer.
 *
 * @param dst    Pointer to the destination buffer.
 * @param dstlen Size of the destination buffer in bytes.
 * @param src    Pointer to the source data to encode.
 * @param srclen Length of the source data in bytes.
 * @return Number of bytes written to dst, or negative on error.
 */
int http2_head_huffman_encode(uint8_t *dst, size_t dstlen, const uint8_t *src, size_t srclen);

/**
 * @brief Initialize a Huffman decoding context to the accepting state.
 *
 * @param ctx Pointer to the decoding context to initialize.
 */
void http2_head_huffman_decode_context_init(http2_hd_huff_decode_context *ctx);

/**
 * @brief Decode Huffman-encoded data incrementally.
 *
 * The context must be initialized by http2_head_huffman_decode_context_init().
 * The caller must set @p fin to nonzero when the given input is the final block.
 * Assumes @p buf has enough room for the decoded output.
 *
 * @param ctx    Pointer to the Huffman decoding context.
 * @param buf    Pointer to the output buffer for decoded bytes.
 * @param src    Pointer to the encoded input data.
 * @param srclen Length of the encoded input in bytes.
 * @param fin    Nonzero if this is the final block of input.
 * @return Number of decoded bytes written, or -1 on failure.
 */
int32_t http2_head_huffman_decode(http2_hd_huff_decode_context *ctx, uint8_t *buf, const uint8_t *src, size_t srclen,
                                  int fin);

/**
 * @brief Check whether the Huffman decoding context is in a failure state.
 *
 * @param ctx Pointer to the decoding context.
 * @return Nonzero if the context indicates a decoding failure, zero otherwise.
 */
int http2_head_huffman_decode_failure_state(http2_hd_huff_decode_context *ctx);
