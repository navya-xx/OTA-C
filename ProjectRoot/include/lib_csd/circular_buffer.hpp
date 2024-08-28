#ifndef CIRCULAR_BUFFER
#define CIRCULAR_BUFFER

#include "pch.hpp"
#include "log_macros.hpp"

class CircularBuffer
{
public:
    CircularBuffer(size_t &capacity);
    bool push(const std::vector<samp_type> &packet, const uhd::time_spec_t &start_time); // push front
    bool pop(std::vector<samp_type> &packet, uhd::time_spec_t &start_time);              // pop last

    void reset();
    void clear();

private:
    std::vector<std::vector<samp_type>> packet_buffer;
    std::vector<uhd::time_spec_t> time_buffer;
    size_t capacity;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
};

class CrossCorrBuffer
{
public:
    CrossCorrBuffer(size_t &capacity, size_t &storage_size);
    bool push(const std::vector<samp_type> &cross_corr_vec, const uhd::time_spec_t &start_time);
    bool pop(std::vector<samp_type> &cross_corr_vec, const size_t &size);

    bool not_enough_space(const size_t &size = 1) const;
    bool not_enough_data(const size_t &size = 1) const;
    void reset();
    void clear();

    uhd::time_spec_t get_time(const size_t &samp_index);

private:
    std::vector<samp_type> corr_buffer;
    std::vector<bool> timer_bool_buffer;
    std::deque<uhd::time_spec_t> timer_storage;
    std::pair<uhd::time_spec_t, size_t> last_pop;

    size_t capacity;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
};

#endif // CIRCULAR_BUFFER