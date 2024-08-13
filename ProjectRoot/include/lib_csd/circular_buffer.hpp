#ifndef CIRCULAR_BUFFER
#define CIRCULAR_BUFFER

#include "pch.hpp"
#include "log_macros.hpp"

template <typename BUFF_DATA_TYPE>
class CircularBuffer
{
public:
    CircularBuffer(size_t &capacity);
    bool push(const BUFF_DATA_TYPE &data); // push front
    bool pop(BUFF_DATA_TYPE &data);        // pop last
    // bool pop_front();                           // remove first element
    // bool push_back(const BUFF_DATA_TYPE &data); // push back

    void resize(const size_t &capacity);
    void reset();
    void clear();
    bool is_empty() const;

private:
    std::vector<BUFF_DATA_TYPE> buffer_;
    size_t capacity_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

template <typename COMPLEX_DATA_TYPE, typename TIME_DATA_TYPE>
class SyncedBufferManager
{
public:
    SyncedBufferManager(size_t buffer_size);

    bool push(const COMPLEX_DATA_TYPE &item1, const TIME_DATA_TYPE &item2);
    bool pop(COMPLEX_DATA_TYPE &item1, TIME_DATA_TYPE &item2);
    // bool pop_front();
    // bool push_back(const COMPLEX_DATA_TYPE &item1, const TIME_DATA_TYPE &item2);

    void resize(const size_t &capacity);
    void reset();
    void clear();

private:
    CircularBuffer<COMPLEX_DATA_TYPE> samples_buffer;
    CircularBuffer<TIME_DATA_TYPE> timer_buffer;
};

template <typename BUFF_DATA_TYPE>
CircularBuffer<BUFF_DATA_TYPE>::CircularBuffer(size_t &capacity) : buffer_(capacity), capacity_(capacity), head_(0), tail_(0)
{
    // Ensure that the capacity is power of 2
    if (capacity & (capacity - 1))
    {
        LOG_ERROR("Buffer capacity must be a power of 2");
    }
};

template <typename BUFF_DATA_TYPE>
void CircularBuffer<BUFF_DATA_TYPE>::resize(const size_t &capacity)
{
    capacity_ = capacity;
    buffer_.resize(capacity);
}

template <typename BUFF_DATA_TYPE>
bool CircularBuffer<BUFF_DATA_TYPE>::push(const BUFF_DATA_TYPE &item)
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

// template <typename BUFF_DATA_TYPE>
// bool CircularBuffer<BUFF_DATA_TYPE>::push_back(const BUFF_DATA_TYPE &item)
// {
//     size_t current_tail = tail_.load(std::memory_order_relaxed);
//     size_t next_tail = (current_tail + 1) & (capacity_ - 1);
//     if (next_tail == head_.load(std::memory_order_acquire))
//     {
//         return false; // Buffer is full
//     }
//     buffer_[current_tail] = item;
//     tail_.store(next_tail, std::memory_order_release);
//     return true;
// }

// template <typename BUFF_DATA_TYPE>
// bool CircularBuffer<BUFF_DATA_TYPE>::pop_front()
// {
//     size_t current_head = head_.load(std::memory_order_relaxed);
//     if (current_head == tail_.load(std::memory_order_acquire))
//     {
//         return false; // Buffer is empty
//     }
//     head_.store((current_head + 1) & (capacity_ - 1), std::memory_order_release);
//     return true;
// }

template <typename BUFF_DATA_TYPE>
void CircularBuffer<BUFF_DATA_TYPE>::reset()
{
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

template <typename BUFF_DATA_TYPE>
void CircularBuffer<BUFF_DATA_TYPE>::clear()
{
    buffer_.clear();
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

template <typename BUFF_DATA_TYPE>
bool CircularBuffer<BUFF_DATA_TYPE>::is_empty() const
{
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
}

template <typename COMPLEX_DATA_TYPE, typename TIME_DATA_TYPE>
SyncedBufferManager<COMPLEX_DATA_TYPE, TIME_DATA_TYPE>::SyncedBufferManager(size_t buffer_size)
    : samples_buffer(buffer_size), timer_buffer(buffer_size)
{
}

template <typename COMPLEX_DATA_TYPE, typename TIME_DATA_TYPE>
bool SyncedBufferManager<COMPLEX_DATA_TYPE, TIME_DATA_TYPE>::push(const COMPLEX_DATA_TYPE &item1, const TIME_DATA_TYPE &item2)
{
    bool pushed1 = samples_buffer.push(item1);
    bool pushed2 = timer_buffer.push(item2);
    if (!pushed1 || !pushed2)
    {
        if (pushed1)
            samples_buffer.push(item1); // Revert push to buffer1 if buffer2 is full
        if (pushed2)
            timer_buffer.push(item2); // Revert push to buffer2 if buffer1 is full
        return false;
    }
    return true;
}

template <typename COMPLEX_DATA_TYPE, typename TIME_DATA_TYPE>
bool SyncedBufferManager<COMPLEX_DATA_TYPE, TIME_DATA_TYPE>::pop(COMPLEX_DATA_TYPE &item1, TIME_DATA_TYPE &item2)
{
    bool popped1 = samples_buffer.pop(item1);
    bool popped2 = timer_buffer.pop(item2);
    if (!popped1 || !popped2)
    {
        return false;
    }
    return true;
}

// template <typename COMPLEX_DATA_TYPE, typename TIME_DATA_TYPE>
// bool SyncedBufferManager<COMPLEX_DATA_TYPE, TIME_DATA_TYPE>::push_back(const COMPLEX_DATA_TYPE &item1, const TIME_DATA_TYPE &item2)
// {
//     bool pushed1 = samples_buffer.push_back(item1);
//     bool pushed2 = timer_buffer.push_back(item2);
//     if (!pushed1 || !pushed2)
//     {
//         if (pushed1)
//             samples_buffer.push_back(item1); // Revert push to buffer1 if buffer2 is full
//         if (pushed2)
//             timer_buffer.push_back(item2); // Revert push to buffer2 if buffer1 is full
//         return false;
//     }
//     return true;
// }

// template <typename COMPLEX_DATA_TYPE, typename TIME_DATA_TYPE>
// bool SyncedBufferManager<COMPLEX_DATA_TYPE, TIME_DATA_TYPE>::pop_front()
// {
//     bool popped_front_1 = samples_buffer.pop_front();
//     bool popped_front_2 = timer_buffer.pop_front();

//     if (!popped_front_1 || !popped_front_2)
//     {
//         return false;
//     }
//     return true;
// }

template <typename COMPLEX_DATA_TYPE, typename TIME_DATA_TYPE>
void SyncedBufferManager<COMPLEX_DATA_TYPE, TIME_DATA_TYPE>::resize(const size_t &capacity)
{
    samples_buffer.resize(capacity);
    timer_buffer.resize(capacity);
}

template <typename COMPLEX_DATA_TYPE, typename TIME_DATA_TYPE>
void SyncedBufferManager<COMPLEX_DATA_TYPE, TIME_DATA_TYPE>::reset()
{
    samples_buffer.reset();
    timer_buffer.reset();
}

template <typename COMPLEX_DATA_TYPE, typename TIME_DATA_TYPE>
void SyncedBufferManager<COMPLEX_DATA_TYPE, TIME_DATA_TYPE>::clear()
{
    samples_buffer.clear();
    timer_buffer.clear();
}

#endif // CIRCULAR_BUFFER