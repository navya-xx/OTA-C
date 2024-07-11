#ifndef OTAC_PROCESSOR
#define OTAC_PROCESSOR

#include "pch.hpp"
#include "config_parser.hpp"

class OtacProcessor
{
public:
    OtacProcessor(ConfigParser &parser, const uhd::time_spec_t &rx_sample_duration);
    void producer();
    void consumer();

private:
    std::vector<std::complex<float>> samples_buffer;
    std::vector<uhd::time_spec_t> timer;

    uhd::time_spec_t rx_sample_duration;
    ConfigParser parser;

    void otac_post_processor(const std::vector<std::complex<float>> &samples);
    void reset();

    size_t capacity;
    size_t front;
    size_t rear;
    size_t num_produced;

    bool update_noise_level = false;

    boost::mutex mtx;
    boost::condition_variable cv_producer;
    boost::condition_variable cv_consumer;
};

#endif // OTAC_PROCESSOR