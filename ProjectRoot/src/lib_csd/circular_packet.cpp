#include "circular_packet.hpp"

CircularPacket::CircularPacket(size_t &capacity) : capacity_(capacity), head_(0), tail_(0)
{
    packet_buffer.resize(capacity);
    time_specs.resize(capacity);
}

bool CircularPacket::push(const std::vector<std::complex<float>> &packet, const uhd::time_spec_t &time_first_sample)
{
    size_t current_head = head_.load(std::memory_order_relaxed);
    size_t next_head = (current_head + 1) & (capacity_ - 1);
    if (next_head == tail_.load(std::memory_order_acquire))
    {
        return false; // Buffer is full
    }
    packet_buffer[current_head] = packet;
    time_specs[current_head] = time_first_sample;
    head_.store(next_head, std::memory_order_release);
    return true;
}

bool CircularPacket::pop(std::vector<std::complex<float>> &packet, uhd::time_spec_t &time_first_sample)
{
    size_t current_tail = tail_.load(std::memory_order_relaxed);
    if (current_tail == head_.load(std::memory_order_acquire))
    {
        return false; // Buffer is empty
    }
    packet = packet_buffer[current_tail];
    time_first_sample = time_specs[current_tail];
    tail_.store((current_tail + 1) & (capacity_ - 1), std::memory_order_release);
    return true;
}

// Compute the timestamp of a sample given its index within the packet
uhd::time_spec_t CircularPacket::get_sample_time(const uhd::time_spec_t &time_first_sample, const int &sample_index, const float &sample_duration)
{
    return time_first_sample + uhd::time_spec_t(sample_index * sample_duration);
}

void CircularPacket::reset()
{
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

void CircularPacket::clear()
{
    packet_buffer.clear();
    time_specs.clear();
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

bool CircularPacket::is_empty() const
{
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
}