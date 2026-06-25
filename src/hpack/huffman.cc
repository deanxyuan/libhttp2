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
 * @file huffman.cc
 * @brief HTTP/2 HPACK Huffman encoding and decoding implementations.
 */

#include "src/hpack/huffman.h"

#include <string.h>
#include <stdio.h>

#include "src/utils/byte_order.h"

size_t http2_head_huffman_encode_count(const uint8_t *src, size_t len) {
    size_t i;
    size_t nbits = 0;

    for (i = 0; i < len; ++i) {
        nbits += huff_sym_table[src[i]].nbits;
    }
    /* pad the prefix of EOS (256) */
    return (nbits + 7) / 8;
}

int http2_head_huffman_encode(uint8_t *dst, size_t dstlen, const uint8_t *src, size_t srclen) {
    const http2_huff_sym *sym;
    const uint8_t *end = src + srclen;
    uint64_t code = 0;
    uint32_t x;
    size_t nbits = 0;
    int rv;
    uint8_t *bufs = dst;
    size_t space = dstlen;

    for (; src != end;) {
        sym = &huff_sym_table[*src++];
        code |= (uint64_t)sym->code << (32 - nbits);
        nbits += sym->nbits;
        if (nbits < 32) {
            continue;
        }
        if (dstlen >= 4) {
            x = change_byte_order((uint32_t)(code >> 32));
            memcpy(bufs, &x, 4);
            bufs += 4;
            dstlen -= 4;
            code <<= 32;
            nbits -= 32;
            continue;
        }

        for (; nbits >= 8;) {
            *bufs++ = (uint8_t)(code >> 56);
            code <<= 8;
            nbits -= 8;
            dstlen--;
        }
    }

    for (; nbits >= 8;) {
        *bufs++ = (uint8_t)(code >> 56);
        code <<= 8;
        nbits -= 8;
        dstlen--;
    }

    if (nbits) {
        *bufs++ = (uint8_t)((uint8_t)(code >> 56) | ((1 << (8 - nbits)) - 1));
        dstlen--;
    }
    return static_cast<int>(space - dstlen);
}

void http2_head_huffman_decode_context_init(http2_hd_huff_decode_context *ctx) {
    ctx->fstate = HTTP2_HUFF_ACCEPTED;
}

int32_t http2_head_huffman_decode(http2_hd_huff_decode_context *ctx, uint8_t *buf, const uint8_t *src, size_t srclen,
                                  int fin) {
    const uint8_t *end = src + srclen;
    http2_huff_decode node = {ctx->fstate, 0};
    const http2_huff_decode *t = &node;
    uint8_t c;
    int32_t bytes = 0;

    /* We use the decoding algorithm described in
       http://graphics.ics.uci.edu/pub/Prefix.pdf */
    for (; src != end;) {
        c = *src++;
        t = &huff_decode_table[t->fstate & 0x1ff][c >> 4];
        if (t->fstate & HTTP2_HUFF_SYM) {
            *buf++ = t->sym;
            bytes++;
        }

        t = &huff_decode_table[t->fstate & 0x1ff][c & 0xf];
        if (t->fstate & HTTP2_HUFF_SYM) {
            *buf++ = t->sym;
            bytes++;
        }
    }

    ctx->fstate = t->fstate;

    if (fin && !(ctx->fstate & HTTP2_HUFF_ACCEPTED)) {
        return -1;
    }

    return bytes;
}

int http2_head_huffman_decode_failure_state(http2_hd_huff_decode_context *ctx) {
    return ctx->fstate == 0x100;
}
