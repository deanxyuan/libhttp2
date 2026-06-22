# Changelog

## v1.0.0

Initial release.

### Features

- Full HTTP/2 framing support (RFC 7540): DATA, HEADERS, PRIORITY, RST_STREAM, SETTINGS, PUSH_PROMISE, PING, GOAWAY, WINDOW_UPDATE, and CONTINUATION frames.
- HPACK header compression and decompression (RFC 7541): static table, dynamic table, Huffman coding, and configurable dynamic table sizing.
- Complete stream state machine (idle, reserved, open, half-closed, closed) per RFC 7540 Section 5.1.
- Connection-level and stream-level flow control with a pluggable `FlowControlHandler` interface.
- Client and server connection preface handling.
- Zero-copy buffer management via reference-counted `slice` and `slice_buffer`.
- Lock-free multi-producer single-consumer queue (MPSC) for async integration patterns.
- Zero external dependencies; self-contained C++11 static library.
- Cross-platform support: Linux, macOS, Windows, iOS, Android.
- Test suite covering byte order, Huffman coding, frame packing/parsing, HPACK encoding/decoding, dynamic table operations, slice buffers, and static table lookups.
