#ifndef CIRCULAR_PACKET
#define CIRCULAR_PACKET

#include "pch.hpp"
#include "log_macros.hpp"

class CircularPacket
{
public:
    CircularPacket(size_t &capacity);
    bool push(const std::vector<std::complex<float>> &packet, const uhd::time_spec_t &time_first_sample); // push front
    bool pop(std::vector<std::complex<float>> &packet, uhd::time_spec_t &time_first_sample);              // pop last
    uhd::time_spec_t get_sample_time(const uhd::time_spec_t &time_first_sample, const int &sample_index, const float &sample_duration);

    void resize(size_t &capacity);
    void reset();
    void clear();
    bool is_empty() const;

private:
    std::vector<std::vector<std::complex<float>>> packet_buffer;
    std::vector<std::shared_ptr<uhd::time_spec_t>> pointerBuffer;
    std::vector<uhd::time_spec_t> time_specs;

    size_t capacity_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

#endif