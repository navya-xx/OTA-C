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
#include "utility_funcs.hpp"
#include "ConfigParser.hpp"

extern const bool DEBUG;

namespace po = boost::program_options;

class USRP_class
{
public:
    USRP_class(const ConfigParser &parser);

    void initialize();

    bool transmission(const std::vector<std::complex<float>> &buff, const uhd::time_spec_t &tx_time);

    std::vector<std::complex<float>> reception(const size_t &num_rx_samps, const uhd::time_spec_t &rx_time);

    bool stop_signal_called = false;

private:
    ConfigParser parser;

    uhd::usrp::multi_usrp::sptr usrp;
    uhd::rx_streamer::sptr rx_streamer;
    uhd::tx_streamer::sptr tx_streamer;
    float tx_rate, rx_rate, tx_gain, rx_gain, tx_bw, rx_bw;

    typedef std::function<uhd::sensor_value_t(const std::string &)> get_sensor_fn_t;

    bool check_locked_sensor(std::vector<std::string> sensor_names,
                             const char *sensor_name,
                             get_sensor_fn_t get_sensor_fn,
                             double setup_time);
};