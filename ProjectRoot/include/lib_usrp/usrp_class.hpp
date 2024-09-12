#ifndef USRP_CLASS
#define USRP_CLASS

#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"
#include "MQTTClient.hpp"

extern const bool DEBUG;

class USRP_class
{
public:
    USRP_class(const ConfigParser &parser);

    void initialize(bool perform_rxtx_test = true);

    // device setup
    void set_device_parameters();
    void set_antenna();
    void set_master_clock_rate();
    void set_sample_rate();
    void set_center_frequency();
    void set_gains();
    void set_bandwidth();
    void apply_additional_settings();

    // additional features
    void print_usrp_device_info();
    std::pair<float, float> query_calibration_data();
    void setup_streamers();
    void perform_rx_tx_tests();
    float get_device_temperature();
    void print_available_sensors();

    // Helper functions
    bool check_and_create_usrp_device();
    void configure_clock_source();
    void publish_usrp_data();
    void log_device_parameters();

    bool transmission(const std::vector<std::complex<float>> &buff, const uhd::time_spec_t &tx_time, bool &stop_signal_called, bool ask_ack = false);

    void continuous_transmission(const std::vector<std::complex<float>> &buff, std::atomic_bool &stop_signal_called);

    std::vector<std::complex<float>> reception(
        bool &stop_signal_called,
        const size_t &num_rx_samps = 0,
        const float &duration = 0.0,
        const uhd::time_spec_t &rx_time = uhd::time_spec_t(0.0),
        bool is_save_to_file = false,
        const std::function<bool(const std::vector<std::complex<float>> &, const size_t &, const uhd::time_spec_t &)> &callback = [](const std::vector<std::complex<float>> &, const size_t &, const uhd::time_spec_t &)
        { return false; });

    void receive_save_with_timer(bool &stop_signal_called, const float &duration);

    void adjust_for_freq_offset(const float &freq_offset);

    void set_tx_gain(const float &_tx_gain, const int &channel = 0);
    void set_rx_gain(const float &_rx_gain, const int &channel = 0);

    std::ofstream rx_save_stream;
    float init_noise_ampl = 0.0;
    size_t max_rx_packet_size, max_tx_packet_size;

    uhd::usrp::multi_usrp::sptr usrp;
    uhd::rx_streamer::sptr rx_streamer;
    uhd::tx_streamer::sptr tx_streamer;
    std::string device_id;
    float tx_rate, rx_rate, tx_gain, tx_pow_ref, rx_gain, rx_pow_ref, tx_bw, rx_bw, carrier_freq, current_temperature;
    uhd::time_spec_t rx_sample_duration, tx_sample_duration, rx_md_time, tx_md_time;
    bool intialize_with_dummy_txrx = true;

    bool external_ref = false;

private:
    ConfigParser parser;

    uhd::sensor_value_t get_sensor_fn_rx(const std::string &sensor_name, const size_t &channel);
    uhd::sensor_value_t get_sensor_fn_tx(const std::string &sensor_name, const size_t &channel);

    bool check_locked_sensor_rx(float setup_time = 1.0);
    bool check_locked_sensor_tx(float setup_time = 1.0);
};

#endif // USRP_CLASS