/**
 * @file slice.cc
 * @brief Implementation of the slice class and related factory functions.
 */

#include "src/utils/slice.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

class slice_refcount final {
public:
    slice_refcount();

    ~slice_refcount() {}

    slice_refcount(const slice_refcount &) = delete;
    slice_refcount &operator=(const slice_refcount &) = delete;

    void ref();

    void unref();

private:
    std::atomic<int32_t> _refs;
};

slice_refcount::slice_refcount() {
    _refs.store(1, std::memory_order_relaxed);
}

void slice_refcount::ref() {
    _refs.fetch_add(1, std::memory_order_relaxed);
}

void slice_refcount::unref() {
    int32_t n = _refs.fetch_sub(1, std::memory_order_acq_rel);
    if (n == 1) {
        free(this);
    }
}

// Sentinel for static (non-owning) slices. Its refcount starts high and will
// never reach zero, so the destructor (which does not call free()) is safe.
// All MakeStaticSlice() calls share this single sentinel.
static slice_refcount s_static_sentinel;

slice::slice(const char *ptr, size_t len) {
    if (len == 0 || ptr == nullptr) {
        _refs = nullptr;
        _length = 0;
        _bytes = nullptr;
        return;
    }

    /*  Memory layout:

        +-----------+----------------------------------------------------------+
        | refcount  | bytes                                                    |
        +-----------+----------------------------------------------------------+

        refcount is a slice_refcount
        bytes is an array of bytes of the requested length
    */
    _refs = (slice_refcount *)malloc(sizeof(slice_refcount) + len);
    new (_refs) slice_refcount();
    _length = len;
    _bytes = reinterpret_cast<uint8_t *>(_refs + 1);
    memcpy(_bytes, ptr, len);
}

slice::slice(const char *ptr)
    : slice(ptr, (ptr ? strlen(ptr) : 0)) {}

slice::slice(const void *ptr, size_t len)
    : slice(reinterpret_cast<const char *>(ptr), len) {}

slice::slice(void *ptr, size_t len)
    : slice((const char *)(ptr), len) {}

slice::slice(const std::string &str)
    : slice(str.data(), str.size()) {}

slice::slice() {
    _refs = nullptr;
    _length = 0;
    _bytes = nullptr;
}

slice::~slice() {
    if (_refs && _refs != &s_static_sentinel) {
        _refs->unref();
    }
}

slice::slice(const slice &oth) {
    if (oth._refs && oth._refs != &s_static_sentinel) {
        oth._refs->ref();
    }
    _refs = oth._refs;
    _length = oth._length;
    _bytes = oth._bytes;
}

slice &slice::operator=(const slice &oth) {
    if (this != &oth) {
        if (_refs && _refs != &s_static_sentinel) {
            _refs->unref();
        }
        if (oth._refs && oth._refs != &s_static_sentinel) {
            oth._refs->ref();
        }
        _refs = oth._refs;
        _length = oth._length;
        _bytes = oth._bytes;
    }
    return *this;
}

slice::slice(slice &&oth) noexcept
    : _refs(oth._refs)
    , _length(oth._length)
    , _bytes(oth._bytes) {
    oth._refs = nullptr;
    oth._length = 0;
    oth._bytes = nullptr;
}

slice &slice::operator=(slice &&oth) noexcept {
    if (this != &oth) {
        if (_refs && _refs != &s_static_sentinel) {
            _refs->unref();
        }
        _refs = oth._refs;
        _length = oth._length;
        _bytes = oth._bytes;
        oth._refs = nullptr;
        oth._length = 0;
        oth._bytes = nullptr;
    }
    return *this;
}

size_t slice::size() const {
    return _length;
}

const uint8_t *slice::data() const {
    return _bytes;
}

uint8_t *slice::mutable_data() {
    return _bytes;
}

void slice::pop_back(size_t remove_size) {
    if (remove_size == 0) {
        return;
    }
    if (remove_size > _length) {
        remove_size = _length;
    }
    _length -= remove_size;
}

void slice::pop_front(size_t remove_size) {
    if (remove_size == 0) {
        return;
    }
    if (remove_size > _length) {
        remove_size = _length;
    }
    _length -= remove_size;
    _bytes += remove_size;
}

std::string slice::to_string() const {
    return std::string(reinterpret_cast<const char *>(data()), size());
}

bool slice::empty() const {
    return (_length == 0);
}

void slice::assign(const std::string &s) {
    slice obj(s);
    this->operator=(obj);
}

bool slice::compare(const std::string &s) const {
    if (s.empty() && empty()) {
        return true;
    }
    if (s.size() != size()) {
        return false;
    }

    return memcmp(data(), s.data(), size()) == 0;
}

bool slice::operator==(const slice &s) const {
    if (s.empty() && empty()) {
        return true;
    }
    if (s.size() != size()) {
        return false;
    }

    return memcmp(data(), s.data(), size()) == 0;
}

slice &slice::operator+=(const slice &s) {
    this->operator=(*this + s);
    return *this;
}

slice MakeStaticSlice(const void *ptr, size_t len) {
    if (len == 0 || ptr == nullptr) return slice();
    slice s;
    s._refs = &s_static_sentinel;
    s._length = len;
    s._bytes = (uint8_t *)ptr;
    return s;
}

slice MakeStaticSlice(const char *ptr) {
    if (!ptr) return slice();
    return MakeStaticSlice(ptr, strlen(ptr));
}

slice MakeSliceByLength(size_t len) {
    if (len == 0) return slice();
    slice s;
    s._refs = (slice_refcount *)malloc(sizeof(slice_refcount) + len);
    new (s._refs) slice_refcount();
    s._length = len;
    s._bytes = reinterpret_cast<uint8_t *>(s._refs + 1);
    return s;
}

slice operator+(slice s1, slice s2) {
    if (s1.empty() && s2.empty()) {
        return slice();
    }
    size_t len = s1.size() + s2.size();
    slice s = MakeSliceByLength(len);
    uint8_t *buff = s.mutable_data();
    if (!s1.empty()) {
        memcpy(buff, s1.data(), s1.size());
        buff += s1.size();
    }
    if (!s2.empty()) {
        memcpy(buff, s2.data(), s2.size());
    }
    return s;
}
