#include "circular_buffer.hpp"

CircularBuffer::CircularBuffer(size_t &capacity) : capacity(capacity), packet_buffer(capacity), time_buffer(capacity), head(0), tail(0) {};

bool CircularBuffer::push(const std::vector<samp_type> &packet, const uhd::time_spec_t &start_time)
{
    size_t current_head = head.load(std::memory_order_relaxed);
    size_t next_head = (current_head + 1) % capacity;
    if (next_head == tail.load(std::memory_order_acquire))
    {
        return false; // Buffer is full
    }
    packet_buffer[current_head] = packet;
    time_buffer[current_head] = start_time;

    head.store(next_head, std::memory_order_release);
    return true;
}

bool CircularBuffer::pop(std::vector<samp_type> &packet, uhd::time_spec_t &start_time)
{
    size_t current_tail = tail.load(std::memory_order_relaxed);
    if (current_tail == head.load(std::memory_order_acquire))
    {
        return false; // Buffer is empty
    }
    packet = packet_buffer[current_tail];
    start_time = time_buffer[current_tail];
    tail.store((current_tail + 1) % capacity, std::memory_order_release);
    return true;
}

void CircularBuffer::reset()
{
    head.store(0, std::memory_order_relaxed);
    tail.store(0, std::memory_order_relaxed);
}

void CircularBuffer::clear()
{
    packet_buffer.clear();
    time_buffer.clear();
    head.store(0, std::memory_order_relaxed);
    tail.store(0, std::memory_order_relaxed);
}

CrossCorrBuffer::CrossCorrBuffer(size_t &capacity, size_t &storage_size) : capacity(capacity), head(0), tail(0), corr_buffer(capacity), timer_bool_buffer(capacity), timer_storage(storage_size) {};

bool CrossCorrBuffer::push(const std::vector<samp_type> &cross_corr_vec, const uhd::time_spec_t &start_time)
{
    size_t vec_len = cross_corr_vec.size();
    if (not_enough_space(vec_len))
        return false;

    timer_storage.push_back(start_time);
    size_t current_head;
    bool first_samp = true;
    for (samp_type samp : cross_corr_vec)
    {
        current_head = head.load(std::memory_order_relaxed);
        corr_buffer[current_head] = samp;
        if (first_samp)
        {
            timer_bool_buffer[current_head] = true;
            first_samp = false;
        }
        else
            timer_bool_buffer[current_head] = false;

        head.store((current_head + 1) % capacity, std::memory_order_release);
    }

    return true;
}

bool CrossCorrBuffer::pop(std::vector<samp_type> &cross_corr_vec, const size_t &size)
{
    if (not_enough_data(size))
        return false;

    size_t current_tail;
    bool timer_set = false;
    for (size_t i = 0; i < size; ++i)
    {
        current_tail = tail.load(std::memory_order_relaxed);
        cross_corr_vec.push_back(corr_buffer[current_tail]);
        if (timer_bool_buffer[current_tail])
        {
            if (timer_set) // timer should be accessed only once
            {
                        }
            last_pop.first = timer_storage.front();
            timer_storage.pop_front();
            last_pop.second = 0;
            timer_set = true;
        }
        else
        {
            ++last_pop.second;
        }
    }
}

bool CrossCorrBuffer::not_enough_space(const size_t &size) const
{
    size_t current_head = head.load(std::memory_order_relaxed);
    size_t current_tail = tail.load(std::memory_order_relaxed);
    return ((current_tail + capacity - current_head) % capacity) < size;
}

bool CrossCorrBuffer::not_enough_data(const size_t &size) const
{
    size_t current_head = head.load(std::memory_order_relaxed);
    size_t current_tail = tail.load(std::memory_order_relaxed);
    return ((current_head + capacity - current_tail) % capacity) < size;
}