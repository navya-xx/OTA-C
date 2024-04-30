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
#include "CycleStartDetector.hpp"
#include "utility_funcs.hpp"

extern const bool DEBUG;

namespace po = boost::program_options;

typedef std::function<uhd::sensor_value_t(const std::string &)> get_sensor_fn_t;

bool check_locked_sensor(std::vector<std::string> sensor_names,
                         const char *sensor_name,
                         get_sensor_fn_t get_sensor_fn,
                         double setup_time);

std::pair<uhd::rx_streamer::sptr, uhd::tx_streamer::sptr> create_usrp_streamers(uhd::usrp::multi_usrp::sptr &usrp, ConfigParser &config_parser);

float get_background_noise_level(uhd::usrp::multi_usrp::sptr &usrp, uhd::rx_streamer::sptr &rx_streamer);

void cyclestartdetector_receiver_thread(CycleStartDetector<std::complex<float>> &csdbuffer, uhd::rx_streamer::sptr rx_stream, std::atomic<bool> &stop_thread_signal, bool &stop_signal_called, const std::chrono::time_point<std::chrono::steady_clock> stop_time, const float rate);

void cyclestartdetector_transmitter_thread(CycleStartDetector<std::complex<float>> &csdbuffer, uhd::rx_streamer::sptr rx_stream, std::atomic<bool> &stop_thread_signal, bool &stop_signal_called, const std::chrono::time_point<std::chrono::steady_clock> &stop_time, const float &rate, uhd::time_spec_t &csd_detect_time, float &csd_ch_pow);