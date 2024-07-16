#include "circular_buffer.hpp"

template <typename BUFF_DATA_TYPE>
CircularBuffer<BUFF_DATA_TYPE>::CircularBuffer(size_t &capacity) : buffer_(capacity), capacity_(capacity), head_(0), tail_(0)
{
    // Ensure that the capacity is power of 2
    if (capacity & (capacity - 1))
    {
        LOG_ERROR("Capacity must be a power of 2");
    }
};

template <typename BUFF_DATA_TYPE>
bool CircularBuffer<BUFF_DATA_TYPE>::push(BUFF_DATA_TYPE item)
{
    size_t current_head = head_.load(std::memory_order_relaxed);
    size_t next_head = (current_head + 1) & (capacity_ - 1);
    if (next_head == tail_.load(std::memory_order_acquire))
    {
        return false; // Buffer is full
    }
    buffer_[current_head] = item;
    head_.store(next_head, std::memory_order_release);
    return true;
}

template <typename BUFF_DATA_TYPE>
bool CircularBuffer<BUFF_DATA_TYPE>::pop(BUFF_DATA_TYPE &item)
{
    size_t current_tail = tail_.load(std::memory_order_relaxed);
    if (current_tail == head_.load(std::memory_order_acquire))
    {
        return false; // Buffer is empty
    }
    item = buffer_[current_tail];
    tail_.store((current_tail + 1) & (capacity_ - 1), std::memory_order_release);
    return true;
}