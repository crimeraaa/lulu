#pragma once

template <class T>
struct Slice {
    T     *data;
    size_t len;

    T &operator[](size_t index)
    {
        if (0 <= index && index < this->len) {
            return this->data[index];
        }
        __builtin_trap();
    }

    T *begin() noexcept
    {
        return this->data;
    }

    T *end() noexcept
    {
        return this->data + this->len;
    }

    const T &operator[](size_t index) const
    {
        if (0 <= index && index < this->len) {
            return this->data[index];
        }
        __builtin_trap();
    }

    const T *begin() const noexcept
    {
        return this->data;
    }

    const T *end() const noexcept
    {
        return this->data + this->len;
    }
};
