#pragma once
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <uhd/convert.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/date_time.hpp>
#include <atomic>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <csignal>
#include <fstream>
#include <thread>
#include <iomanip>
#include <sstream>
#include <deque>
#include <numeric>
#include <algorithm>
#include "utility_funcs.hpp"
#include "ConfigParser.hpp"
#include "waveforms.hpp"

extern const bool DEBUG;

namespace po = boost::program_options;

class USRP_class
{
public:
    USRP_class(const ConfigParser &parser);

    void initialize();

    bool transmission(const std::vector<std::complex<float>> &buff, const uhd::time_spec_t &tx_time, bool ask_ack = true);

    // void gain_adjustment(const float &est_ch_amp, const float &min_path_loss_dB);

    std::vector<std::complex<float>> reception(
        const size_t &num_rx_samps = 0,
        const float &duration = 0.0,
        const uhd::time_spec_t &rx_time = uhd::time_spec_t(0.0),
        std::string filename = "",
        bool is_save_to_file = false);

    bool stop_signal_called = false;
    float init_background_noise;
    size_t max_rx_packet_size, max_tx_packet_size;

    uhd::usrp::multi_usrp::sptr usrp;
    uhd::rx_streamer::sptr rx_streamer;
    uhd::tx_streamer::sptr tx_streamer;
    float tx_rate, rx_rate, tx_gain, rx_gain, tx_bw, rx_bw, carrier_freq;
    uhd::time_spec_t rx_sample_duration, tx_sample_duration, rx_md_time, tx_md_time;

private:
    ConfigParser parser;

    WaveformGenerator wf_gen;

    uhd::sensor_value_t get_sensor_fn_rx(const std::string &sensor_name, const size_t &channel);
    uhd::sensor_value_t get_sensor_fn_tx(const std::string &sensor_name, const size_t &channel);

    bool check_locked_sensor_rx(float setup_time = 1.0);
    bool check_locked_sensor_tx(float setup_time = 1.0);
};