#ifndef USRP_INIT
#define USRP_INIT

#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"

extern const bool DEBUG;

class USRP_init
{
public:
    USRP_init(const ConfigParser &parser);
    uhd::usrp::multi_usrp::sptr usrp;

    void initialize(bool perform_rxtx_test = true);

    // Query function
    uhd::time_spec_t get_time_now();
    bool get_last_cfo();
    void update_cfo(const float &cfo);

    size_t max_rx_packet_size, max_tx_packet_size;

    uhd::rx_streamer::sptr rx_streamer;
    uhd::tx_streamer::sptr tx_streamer;
    std::string device_id;
    float master_clock_rate, tx_rate, rx_rate, tx_gain, tx_pow_ref, rx_gain, rx_pow_ref, tx_bw, rx_bw, carrier_freq, current_temperature, cfo;
    uhd::time_spec_t rx_sample_duration, tx_sample_duration, rx_md_time, tx_md_time;
    bool intialize_with_dummy_txrx = true, flag_correct_cfo = false;

    bool external_ref = false;
    bool use_calib_gains = false;

private:
    ConfigParser parser;

    uhd::sensor_value_t get_sensor_fn_rx(const std::string &sensor_name, const size_t &channel);
    uhd::sensor_value_t get_sensor_fn_tx(const std::string &sensor_name, const size_t &channel);

    bool check_locked_sensor_rx(float setup_time = 1.0);
    bool check_locked_sensor_tx(float setup_time = 1.0);

    // device setup
    void set_device_parameters();
    void set_antenna();
    void set_master_clock_rate();
    void set_sample_rate();
    void set_center_frequency();
    void set_initial_gains();
    void set_bandwidth();
    void apply_additional_settings();
    void set_tx_gain(const float &_tx_gain, const int &channel = 0);
    void set_rx_gain(const float &_rx_gain, const int &channel = 0);

    // additional features
    void print_usrp_device_info();
    std::pair<float, float> query_calibration_data();
    void setup_streamers();
    float get_device_temperature();
    void print_available_sensors();

    // Helper functions
    bool check_and_create_usrp_device();
    void configure_clock_source();
    void log_device_parameters();
    float get_gain(const std::string &trans_type, const bool &get_calib_gains = true);
};

#endif // USRP_INIT