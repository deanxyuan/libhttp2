/**
 * @file slice_buffer.cc
 * @brief Implementation of the slice_buffer class.
 */

#include "src/utils/slice_buffer.h"
#include <assert.h>
#include <string.h>
#include <algorithm>

/** @brief Default constructor. Initializes the byte length counter to zero. */
slice_buffer::slice_buffer()
    : _length(0) {}

/** @brief Destructor. */
slice_buffer::~slice_buffer() {}

/** @brief Merges all stored slices into a single contiguous slice. */
slice slice_buffer::merge() const {

    if (slice_count() == 0) {
        return slice();
    }

    if (slice_count() == 1) {
        return _vs[0];
    }

    size_t length = get_buffer_length();
    slice obj = MakeSliceByLength(length);
    uint8_t *buf = const_cast<uint8_t *>(obj.data());
    for (auto it = _vs.begin(); it != _vs.end(); ++it) {
        memcpy(buf, it->data(), it->size());
        buf += it->size();
    }
    return obj;
}

/** @brief Returns the number of slices in the internal vector. */
size_t slice_buffer::slice_count() const {
    return _vs.size();
}

/** @brief Returns the accumulated byte length of all slices. */
size_t slice_buffer::get_buffer_length() const {
    return _length;
}

/** @brief Appends a const slice reference to the buffer. */
void slice_buffer::add_slice(const slice &s) {
    if (!s.empty()) {
        _vs.emplace_back(s);
        _length += s.size();
    }
}

/** @brief Appends a slice by moving it into the buffer. */
void slice_buffer::add_slice(slice &&s) {
    if (!s.empty()) {
        _vs.emplace_back(s);
        _length += s.size();
    }
}

/** @brief Extracts the first len bytes as a new contiguous slice. */
slice slice_buffer::get_header(size_t len) {
    if (len == 0 || get_buffer_length() < len) {
        return slice();
    }

    slice s = MakeSliceByLength(len);
    copy_to_buffer(const_cast<uint8_t *>(s.data()), len);
    return s;
}

/** @brief Discards the first len bytes from the buffer. */
bool slice_buffer::move_header(size_t len) {
    if (get_buffer_length() < len) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    auto it = _vs.begin();
    size_t left = it->size();

    if (left > len) {
        it->pop_front(len);
        _length -= len;
    } else if (left == len) {
        _length -= len;
        _vs.erase(it);
    } else {
        // len > left
        _length -= left;
        _vs.erase(it);

        return move_header(len - left);
    }
    return true;
}

/** @brief Copies up to length bytes from the buffer into the destination pointer. */
size_t slice_buffer::copy_to_buffer(void *buffer, size_t length) {
    assert(length <= get_buffer_length());

    auto it = _vs.begin();

    size_t left = length;
    size_t pos = 0;
    uint8_t *temp = static_cast<uint8_t *>(buffer);

    while (it != _vs.end() && left != 0) {

        size_t len = std::min(left, it->size());
        memcpy(temp + pos, it->data(), len);

        left -= len;
        pos += len;

        ++it;
    }

    return pos;
}

/** @brief Clears all slices and resets the byte length to zero. */
void slice_buffer::clear_buffer() {
    _vs.clear();
    _length = 0;
}

/** @brief Returns true if the buffer contains no slices. */
bool slice_buffer::empty() const {
    return _vs.empty();
}

/** @brief Returns a const reference to the first slice. Asserts non-empty. */
const slice &slice_buffer::front() const {
    size_t n = _vs.size();
    assert(n > 0);
    return _vs[0];
}

/** @brief Returns a const reference to the last slice. Asserts non-empty. */
const slice &slice_buffer::back() const {
    size_t n = _vs.size();
    assert(n > 0);
    return _vs[n - 1];
}

/** @brief Removes the first slice from the buffer. */
void slice_buffer::pop_front() {
    if (!_vs.empty()) {
        auto it = _vs.begin();
        _length -= it->size();
        _vs.erase(it);
    }
}

/** @brief Removes the last slice from the buffer. */
void slice_buffer::pop_back() {
    size_t c = _vs.size();
    if (c != 0) {
        auto it = _vs.begin();
        std::advance(it, c - 1);
        _length -= it->size();
        _vs.erase(it);
    }
}

/** @brief Accesses a slice by index. Asserts the index is in range. */
const slice &slice_buffer::operator[](size_t i) const {
    assert(i < _vs.size());
    return _vs[i];
}
