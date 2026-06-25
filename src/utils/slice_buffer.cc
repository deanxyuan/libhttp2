/**
 * @file slice_buffer.cc
 * @brief Implementation of the slice_buffer class.
 */

#include "src/utils/slice_buffer.h"
#include <assert.h>
#include <string.h>
#include <algorithm>

slice_buffer::slice_buffer()
    : _start(0)
    , _length(0) {}

slice_buffer::~slice_buffer() {}

slice slice_buffer::merge() const {
    size_t count = slice_count();
    if (count == 0) {
        return slice();
    }

    if (count == 1) {
        return _vs[_start];
    }

    size_t length = get_buffer_length();
    slice obj = MakeSliceByLength(length);
    uint8_t *buf = obj.mutable_data();
    for (size_t i = _start; i < _vs.size(); ++i) {
        memcpy(buf, _vs[i].data(), _vs[i].size());
        buf += _vs[i].size();
    }
    return obj;
}

size_t slice_buffer::slice_count() const {
    return _vs.size() - _start;
}

size_t slice_buffer::get_buffer_length() const {
    return _length;
}

void slice_buffer::add_slice(const slice &s) {
    if (!s.empty()) {
        _vs.emplace_back(s);
        _length += s.size();
    }
}

void slice_buffer::add_slice(slice &&s) {
    if (!s.empty()) {
        _vs.emplace_back(s);
        _length += s.size();
    }
}

slice slice_buffer::get_header(size_t len) {
    if (len == 0 || get_buffer_length() < len) {
        return slice();
    }

    slice s = MakeSliceByLength(len);
    copy_to_buffer(s.mutable_data(), len);
    return s;
}

bool slice_buffer::move_header(size_t len) {
    if (get_buffer_length() < len) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    size_t remaining = len;
    while (remaining > 0 && _start < _vs.size()) {
        size_t front_size = _vs[_start].size();
        if (front_size > remaining) {
            _vs[_start].pop_front(remaining);
            _length -= remaining;
            remaining = 0;
        } else {
            _length -= front_size;
            remaining -= front_size;
            ++_start;
        }
    }

    _compact();
    return true;
}

size_t slice_buffer::copy_to_buffer(void *buffer, size_t length) {
    assert(length <= get_buffer_length());

    size_t left = length;
    size_t pos = 0;
    uint8_t *temp = static_cast<uint8_t *>(buffer);

    for (size_t i = _start; i < _vs.size() && left != 0; ++i) {
        size_t len = std::min(left, _vs[i].size());
        memcpy(temp + pos, _vs[i].data(), len);

        left -= len;
        pos += len;
    }

    return pos;
}

void slice_buffer::clear_buffer() {
    _vs.clear();
    _start = 0;
    _length = 0;
}

bool slice_buffer::empty() const {
    return (_start >= _vs.size());
}

const slice &slice_buffer::front() const {
    assert(_start < _vs.size());
    return _vs[_start];
}

const slice &slice_buffer::back() const {
    assert(!_vs.empty());
    return _vs.back();
}

void slice_buffer::pop_front() {
    if (_start < _vs.size()) {
        _length -= _vs[_start].size();
        ++_start;
        _compact();
    }
}

void slice_buffer::pop_back() {
    if (_start < _vs.size()) {
        _length -= _vs.back().size();
        _vs.pop_back();
    }
}

const slice &slice_buffer::operator[](size_t i) const {
    assert(_start + i < _vs.size());
    return _vs[_start + i];
}

void slice_buffer::_compact() {
    if (_start > 0 && (_start >= _vs.size() || _start > _vs.size() / 2)) {
        _vs.erase(_vs.begin(), _vs.begin() + _start);
        _start = 0;
    }
}
