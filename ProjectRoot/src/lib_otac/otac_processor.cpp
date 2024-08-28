#include "otac_processor.hpp"

OtacProcessor::OtacProcessor(ConfigParser &parser,
                             const uhd::time_spec_t &rx_sample_duration) : parser(parser),
                                                                           rx_sample_duration(rx_sample_duration),
{
    front = 0;
    rear = 0;
    num_produced = 0;

    size_t max_rx_packet_size = parser.getValue_int("max-rx-packet-size");
    capacity = max_rx_packet_size * parser.getValue_int("capacity-mul");

    samples_buffer.resize(capacity, samp_type(0.0, 0.0));
};

void OtacProcessor::producer()
{
}