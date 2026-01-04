
#pragma once
#include <vector>
#include <cstddef>

template <typename T>
class RingBuffer
{
public:
    explicit RingBuffer(std::size_t capacity)
        : buf_(capacity), cap_(capacity) {}

    bool push(T &&v)
    {
        if (cap_ == 0)
            return false;
        if (size_ == cap_)
            return false; // full
        buf_[tail_] = std::move(v);
        tail_ = (tail_ + 1) % cap_;
        ++size_;
        return true;
    }

    bool pop(T &out)
    {
        if (size_ == 0)
            return false;
        out = std::move(buf_[head_]);
        head_ = (head_ + 1) % cap_;
        --size_;
        return true;
    }

    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == cap_; }
    std::size_t size() const { return size_; }
    std::size_t capacity() const { return cap_; }

private:
    std::vector<T> buf_;
    std::size_t cap_ = 0;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;
};
