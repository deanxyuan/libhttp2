/**
 * @file slice.cc
 * @brief Implementation of the slice class and related factory functions.
 */

#include "src/utils/slice.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>

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

slice::slice(const char *ptr, size_t len) {
    if (len == 0) {  // empty object
        _refs = nullptr;
        memset(&_data, 0, sizeof(_data));
        return;
    }

    if (len <= SLICE_INLINED_SIZE) {
        _refs = nullptr;
        _data.inlined.length = static_cast<uint8_t>(len);
        if (ptr) {
            memcpy(_data.inlined.bytes, ptr, len);
        }
    } else {
        /*  Memory layout used by the slice created here:

            +-----------+----------------------------------------------------------+
            | refcount  | bytes                                                    |
            +-----------+----------------------------------------------------------+

            refcount is a slice_refcount
            bytes is an array of bytes of the requested length
        */

        _refs = (slice_refcount *)malloc(sizeof(slice_refcount) + len);
        new (_refs) slice_refcount();
        _data.refcounted.length = len;
        _data.refcounted.bytes = reinterpret_cast<uint8_t *>(_refs + 1);
        if (ptr) {
            memcpy(_data.refcounted.bytes, ptr, len);
        }
    }
}

slice::slice(const char *ptr)
    : slice(ptr, strlen(ptr)) {}

slice::slice(const void *ptr, size_t len)
    : slice(reinterpret_cast<const char *>(ptr), len) {}

slice::slice(void *ptr, size_t len)
    : slice((const char *)(ptr), len) {}

slice::slice(const std::string &str)
    : slice(str.data(), str.size()) {}

slice::slice() {
    _refs = nullptr;
    memset(&_data, 0, sizeof(_data));
}

slice::~slice() {
    if (_refs) {
        _refs->unref();
    }
}

slice::slice(const slice &oth) {
    if (oth._refs != nullptr) {
        oth._refs->ref();
    }
    _refs = oth._refs;
    _data = oth._data;
}

slice &slice::operator=(const slice &oth) {
    if (this != &oth) {
        if (_refs) {
            _refs->unref();
        }
        if (oth._refs) {
            oth._refs->ref();
        }
        _refs = oth._refs;
        _data = oth._data;
    }
    return *this;
}

slice::slice(slice &&oth) noexcept
    : _refs(oth._refs)
    , _data(oth._data) {
    oth._refs = nullptr;
    memset(&oth._data, 0, sizeof(oth._data));
}

slice &slice::operator=(slice &&oth) noexcept {
    if (this != &oth) {
        if (_refs) {
            _refs->unref();
        }
        _refs = oth._refs;
        _data = oth._data;
        oth._refs = nullptr;
        memset(&oth._data, 0, sizeof(oth._data));
    }
    return *this;
}

size_t slice::size() const {
    return (_refs) ? _data.refcounted.length : _data.inlined.length;
}

const uint8_t *slice::data() const {
    return (_refs) ? _data.refcounted.bytes : _data.inlined.bytes;
}

void slice::pop_back(size_t remove_size) {
    if (remove_size == 0) {
        return;
    }
    if (remove_size > size()) {
        remove_size = size();
    }

    if (_refs) {
        _data.refcounted.length -= remove_size;
    } else {
        _data.inlined.length -= static_cast<uint8_t>(remove_size);
    }
}

void slice::pop_front(size_t remove_size) {
    if (remove_size == 0) {
        return;
    }

    if (remove_size > size()) {
        remove_size = size();
    }

    if (_refs) {
        _data.refcounted.length -= remove_size;
        _data.refcounted.bytes += remove_size;
    } else {
        _data.inlined.length -= static_cast<uint8_t>(remove_size);
        memmove(_data.inlined.bytes, _data.inlined.bytes + remove_size, _data.inlined.length);
    }
}

std::string slice::to_string() const {
    return std::string(reinterpret_cast<const char *>(data()), size());
}

bool slice::empty() const {
    return (size() == 0);
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

std::once_flag of;

slice MakeStaticSlice(const void *ptr, size_t len) {
    if (len == 0) return slice();
    static slice_refcount *refs = nullptr;
    std::call_once(of, [](void) { refs = new slice_refcount(); });
    slice s;
    s._refs = refs;
    s._data.refcounted.bytes = (uint8_t *)ptr;
    s._data.refcounted.length = len;
    s._refs->ref();
    return s;
}

slice MakeStaticSlice(const char *ptr) {
    if (!ptr) return slice();
    return MakeStaticSlice(ptr, strlen(ptr));
}

slice MakeSliceByLength(size_t len) {
    slice s;
    if (len <= slice::SLICE_INLINED_SIZE) {
        s._refs = nullptr;
        s._data.inlined.length = static_cast<uint8_t>(len);
    } else {
        s._refs = (slice_refcount *)malloc(sizeof(slice_refcount) + len);
        new (s._refs) slice_refcount();
        s._data.refcounted.length = len;
        s._data.refcounted.bytes = reinterpret_cast<uint8_t *>(s._refs + 1);
    }
    return s;
}

slice operator+(slice s1, slice s2) {
    if (s1.empty() && s2.empty()) {
        return slice();
    }
    size_t len = s1.size() + s2.size();
    slice s = MakeSliceByLength(len);
    uint8_t *buff = const_cast<uint8_t *>(s.data());
    if (!s1.empty()) {
        memcpy(buff, s1.data(), s1.size());
        buff += s1.size();
    }
    if (!s2.empty()) {
        memcpy(buff, s2.data(), s2.size());
    }
    return s;
}
