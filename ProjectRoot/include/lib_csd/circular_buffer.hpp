#ifndef CIRCULAR_BUFFER
#define CIRCULAR_BUFFER

#include "pch.hpp"
#include "log_macros.hpp"

template <typename BUFF_DATA_TYPE>
class CircularBuffer
{
public:
    CircularBuffer(size_t &capacity);
    bool push(BUFF_DATA_TYPE data);
    bool pop(BUFF_DATA_TYPE &data);

private:
    std::vector<BUFF_DATA_TYPE> buffer_;
    size_t capacity_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

#endif // CIRCULAR_BUFFER