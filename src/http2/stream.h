#pragma once
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "src/hpack/metadata.h"
#include "src/utils/slice_buffer.h"
#include "src/http2/frame.h"

#include "http2/transport.h"

class http2_stream : public http2::Stream, public std::enable_shared_from_this<http2_stream> {
public:
    http2_stream(uint64_t connection_id, uint32_t stream_id);
    ~http2_stream() {}

    // action
    void send_push_promise();
    void recv_push_promise();
    void send_headers();
    void recv_headers(std::vector<hpack::mdelem_data> &headers);
    void send_rst_stream();
    void recv_rst_stream(uint32_t error_code);
    void send_end_stream();
    void recv_end_stream();

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

    uint64_t ConnectionId() override;
    uint32_t StreamId() override;
    int32_t Weight() override;  // Priority
    bool Exclusive() override;  // Priority
    int32_t Flags() override;
    uint32_t ErrorCode() override;
    int CurrentState() override;
    std::multimap<std::string, std::string> GetHeaders() override;
    uint32_t GetDataBlock(uint32_t (*parse_func)(const uint8_t *ptr, uint32_t len)) override;
    void PopDataBlock(uint8_t *output, uint32_t size) override;

private:
    uint64_t _connection_id;
    uint32_t _stream_id;

    int _state;

    uint8_t _frame_flags;
    uint8_t _frame_type;

    bool _read_closed;
    bool _write_closed;

    http2_priority_spec _spec;

    uint32_t _last_error;
    slice_buffer _data_cache;
    std::vector<hpack::mdelem_data> _headers;
};
