#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <string>

class slice_refcount;
class slice final {
public:
    slice();
    slice(const char *ptr);
    slice(const std::string &str);
    slice(const void *ptr, size_t length);
    slice(void *ptr, size_t length);
    slice(const char *ptr, size_t length);
    ~slice();

    slice(const slice &oth);
    slice &operator=(const slice &oth);

    slice(slice &&oth);
    slice &operator=(slice &&oth);

    size_t size() const;
    const uint8_t *data() const;
    void pop_back(size_t remove_size);
    void pop_front(size_t remove_size);

    std::string to_string() const;
    bool empty() const;

    void assign(const std::string &s);
    bool compare(const std::string &s) const;

    bool operator==(const slice &oth) const;
    bool operator!=(const slice &&oth) const {
        return !(this->operator==(oth));
    }

    slice &operator+=(const slice &s);

private:
    enum { SLICE_INLINED_SIZE = 23 };

    slice_refcount *_refs;
    union slice_data {
        struct {
            size_t length;
            uint8_t *bytes;
        } refcounted;
        struct {
            uint8_t length;
            uint8_t bytes[23];
        } inlined;
    } _data;

    friend slice MakeStaticSlice(const void *ptr, size_t len);
    friend slice MakeSliceByLength(size_t len);
    friend slice operator+(slice s1, slice s2);
};

// Static slice will not copy data, save the pointer directly.
slice MakeStaticSlice(const void *ptr, size_t len);
slice MakeStaticSlice(const char *ptr);

slice MakeSliceByLength(size_t len);
slice operator+(slice s1, slice s2);
