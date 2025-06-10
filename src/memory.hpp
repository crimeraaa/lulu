#pragma once

#include "lulu.hpp"
#include <cassert>

template <class T>
struct Slice {
    T     *m_data = nullptr;
    size_t m_len  = 0;
    
    // Read-only access.
    const T &operator[](size_t index) const {
        assert(index < m_len && "Index out of bounds");
        return m_data[index];
    };
    
    // Read-write access.
    T &operator[](size_t index) {
        assert(index < m_len && "Index out of bounds");
        return m_data[index];
    }

    T *begin() {
        return m_data;
    }
    
    T *end() {
        return m_data + m_len;
    }

    const T *begin() const noexcept {
        return m_data;
    }
    
    const T *end() const noexcept {
        return m_data + m_len;
    }
};

template <class T>
struct Dynamic {
    T     *m_data = nullptr;
    size_t m_len  = 0;
    size_t m_cap  = 0;

    // Read-only access.
    const T &operator[](size_t index) const {
        assert(index < m_len && "Index out of bounds");
        return m_data[index];
    };
    
    // Read-write access.
    T &operator[](size_t index) {
        assert((index < m_len) && "Index out of bounds");
        return m_data[index];
    }
    
    const T *begin() const noexcept {
        return m_data;
    }
    
    const T *end() const noexcept {
        return m_data + m_len;
    }
};

struct Raw_Dynamic {
    void  *data = nullptr;
    size_t len  = 0;
    size_t cap  = 0;
};

void *
raw_dynamic_grow(lulu_VM &vm, Raw_Dynamic &self, size_t type_size);

void *
mem_realloc(lulu_VM &vm, void *ptr, size_t old_size, size_t new_size);

template<class T>
void
dynamic_push(lulu_VM &vm, Dynamic<T> &self, const T &value)
{
    if (self.m_len >= self.m_cap) {
        Raw_Dynamic &hack = reinterpret_cast<Raw_Dynamic &>(self);
        self.m_data = static_cast<T *>(raw_dynamic_grow(vm, hack, sizeof(T)));
    }
    self.m_data[self.m_len++] = value;
}

template<class T>
void
dynamic_destroy(lulu_VM &vm, Dynamic<T> &self)
{
    mem_realloc(vm, self.m_data, sizeof(T) * self.m_cap, 0);
}
