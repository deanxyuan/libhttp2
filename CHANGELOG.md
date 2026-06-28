# Changelog

## v1.0.1 (2025-06-28)

### Security Fixes

- **HPACK integer decode overflow**: Add bounds checking to prevent crafted continuation bytes from causing integer overflow/wraparound in HPACK integer decoding.
- **HPACK pointer arithmetic overflow**: Fix `buf + str_len` wraparound in plain-string bounds check that could enable OOB reads.
- **HPACK resize multiplication overflow**: Guard against `len * 3` overflow on 32-bit platforms when allocating Huffman decode buffer.
- **Parser OOB reads**: Add bounds checks for HEADERS (Priority flag + short payload) and PUSH_PROMISE (payload < 4 bytes) to prevent out-of-bounds memory access.
- **Parser return values**: Check parse results in all 10 frame dispatch functions; previously uninitialized struct fields could be read after parse failures.

### Protocol Compliance Fixes

- **DATA on stream 0**: Send PROTOCOL_ERROR when DATA frames are received with stream identifier 0 (RFC 7540 §6.1).
- **RST_STREAM on idle stream**: Send PROTOCOL_ERROR when RST_STREAM targets an unknown/idle stream (RFC 7540 §6.4).
- **HEADERS state validation**: Reject HEADERS frames on streams not in Idle state (RFC 7540 §5.1).
- **GOAWAY stream marking**: Fix inverted logic — streams with ID > last_stream_id are now correctly marked unwritable (RFC 7540 §6.8).
- **Send-side flow control**: Enforce connection-level send window; prevent sending beyond the remote peer's advertised window (RFC 7540 §6.9.1).
- **MAX_CONCURRENT_STREAMS**: Enforce the remote peer's SETTINGS_MAX_CONCURRENT_STREAMS limit (RFC 7540 §5.1.2).
- **MaxFrameSize direction**: Use remote peer's MaxFrameSize when sending DATA frames, not local setting.
- **CONTINUATION after PUSH_PROMISE**: Fix stream ID tracking so CONTINUATION frames are validated against the correct (associated) stream ID (RFC 7540 §6.10).

### Bug Fixes

- **Stream state machine**: Fix Recv PUSH_PROMISE transition from Idle → ReservedRemote (was incorrectly HalfClosedRemote).
- **Empty DATA END_STREAM**: Process END_STREAM flag on empty DATA frames (receive side) and allow sending empty DATA+END_STREAM frames (send side).
- **ReadData memory leak**: `ReadData()` now properly consumes data from the buffer after copying, preventing unbounded growth.
- **flush_buffer data loss**: Only clear send buffer on successful flush; preserve data on send failure.
- **send_goaway/send_rst_stream preface**: Ensure HTTP/2 client connection preface is sent before GOAWAY and RST_STREAM frames.
- **Stream send return values**: `stream_send_headers`/`stream_send_data` now return `false` on actual send failure instead of always returning `true`.
- **HPACK static table**: Remove unused gRPC static table extension (entries 62-85) and fix bounds-check logic.
- **Send window type**: Widen `_connection_send_window` from `int32_t` to `int64_t` to prevent overflow.
- **assert → runtime check**: Replace debug-only assertions with runtime checks in `rebuild_elems`.
- **compressor_destroy leak**: Release all slice references in cuckoo hash table on compressor destruction.
- **memcpy(NULL,0)**: Guard `memcpy` calls with `if (size > 0)` where source pointer may be NULL.
- **date.h thread safety**: Use `gmtime_s` (Windows) / `gmtime_r` (POSIX) instead of non-thread-safe `gmtime`.
- **date.h strftime format**: Fix `%G` → `%Y` (ISO week-year → calendar year).
- **INT_MASK signed overflow**: Change `1 << bits` to `1u << bits` to avoid UB.
- **Macro do-while safety**: Add guards to `ROTL`/`ROTR`/`ROTL32`/`FMIX32` macros.
- **Assertion type mismatch**: Fix `table_elems < max_table_size` → `table_elems < max_table_elems`.

### Internals

- Add `CurrentStateTyped()` and `ScopedBufferedMode` RAII guard to public API.
- Add debug-mode `assert_same_thread()` thread-affinity checks.
- Add `test_protocol.cc`, `test_integration.cc`, `test_integration_ext.cc`, `test_multi_stream_crash.cc` integration test suites.
- Reimplement `move_header` with iteration instead of recursion to prevent stack overflow.
- Fix strict aliasing UB in `byte_order` utilities.

## v1.0.0

Initial release.
