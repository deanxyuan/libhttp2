#pragma once
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "src/hpack/metadata.h"
#include "src/utils/slice_buffer.h"

#include "http2/transport.h"

class http2_stream : public http2::Stream, public std::enable_shared_from_this<http2_stream> {
public:
    http2_stream(uint64_t connection_id, uint32_t stream_id);
    ~http2_stream() {}

    // action
    void send_push_promise();
    void recv_push_promise();
    void send_headers();
    void recv_headers(const std::vector<hpack::mdelem_data> &headers);
    void send_rst_stream();
    void recv_rst_stream(uint32_t error_code);
    void send_end_stream();
    void recv_end_stream();

    // test
    bool try_send_push_promise();
    bool try_send_headers();
    bool try_send_rst_stream();
    bool try_send_end_stream();

    uint8_t frame_type();
    uint8_t frame_flags();

    int get_state() const;

    void append_headers(const std::vector<hpack::mdelem_data> &headers);
    void append_data(slice s);

    bool is_closed() const;
    uint32_t stream_id() const;

    void set_priority_info(http2_priority_spec *info);
    void save_frame_info(http2_frame_hdr *hdr);

    void mark_unwritable();
    void mark_unreadable();

    std::shared_ptr<http2::Stream> get_shared_stream();

private:
    uint64_t _connection_id;
    uint32_t _stream_id;

    int _current_state;

    uint8_t _frame_flags;
    uint8_t _frame_type;

    bool _finish_header;  // END_HEADERS
    bool _read_closed;
    bool _write_closed;

    bool _received_eos;
    bool _sent_eos;

    int32_t _weight;

    uint32_t _last_error;
    slice_buffer _data_cache;
    std::vector<hpack::mdelem_data> _headers;
};
