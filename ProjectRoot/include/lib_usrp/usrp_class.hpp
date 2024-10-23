#ifndef USRP_CLASS
#define USRP_CLASS

#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"
#include "usrp_init.hpp"
#include "MQTTClient.hpp"

extern const bool DEBUG;

class USRP_class : public USRP_init
{
public:
    USRP_class(const ConfigParser &parser);

    float estimate_background_noise_power(const size_t &num_pkts = 100);
    void publish_usrp_data();

    bool transmission(const std::vector<sample_type> &buff, const uhd::time_spec_t &tx_time, bool &stop_signal_called, bool ask_ack = false);

    bool single_burst_transmission(const std::vector<sample_type> &buff, const uhd::time_spec_t &tx_time, bool &stop_signal_called, bool ask_ack = false);

    void continuous_transmission(const std::vector<sample_type> &buff, std::atomic_bool &stop_signal_called);

    std::vector<sample_type> reception(
        bool &stop_signal_called,
        const size_t &num_rx_samps = 0,
        const float &duration = 0.0,
        const uhd::time_spec_t &rx_time = uhd::time_spec_t(0.0),
        bool is_save_to_file = false,
        const std::function<bool(const std::vector<sample_type> &, const size_t &, const uhd::time_spec_t &)> &callback = [](const std::vector<sample_type> &, const size_t &, const uhd::time_spec_t &)
        { return false; });

    void receive_save_with_timer(bool &stop_signal_called, const float &duration);
    void receive_fixed_num_samps(bool &stop_signal_called, const size_t &num_rx_samples, std::vector<sample_type> &out_samples, uhd::time_spec_t &out_timer);
    void receive_continuously_with_callback(bool &stop_signal_called, const std::function<bool(const std::vector<sample_type> &, const size_t &, const uhd::time_spec_t &)> &callback = [](const std::vector<sample_type> &, const size_t &, const uhd::time_spec_t &)
                                                                      { return false; });

    bool cycleStartDetector(bool &stop_signal_called, uhd::time_spec_t &ref_timer, float &ref_sig_power, const float &max_duration = 120);

    void lowpassFiltering(const std::vector<sample_type> &rx_samples, std::vector<sample_type> &decimated_samples);

    std::ofstream rx_save_stream;
    float init_noise_ampl = 0.0;

private:
    ConfigParser parser;

    void pre_process_tx_symbols(std::vector<sample_type> &tx_samples, const float &scale = 1.0);
    void post_process_rx_symbols(std::vector<sample_type> &rx_ramples);
};

#endif // USRP_CLASS