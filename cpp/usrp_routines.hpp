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
#include "ConfigParser.hpp"
#include "USRP_class.hpp"

extern const bool DEBUG;

namespace po = boost::program_options;

void cyclestartdetector_receiver_thread(CycleStartDetector &csdbuffer, uhd::rx_streamer::sptr rx_stream, std::atomic<bool> &stop_thread_signal, bool &stop_signal_called, const std::chrono::time_point<std::chrono::steady_clock> &stop_time, const float &rate);

void cyclestartdetector_transmitter_thread(CycleStartDetector &csdbuffer, uhd::rx_streamer::sptr rx_stream, std::atomic<bool> &stop_thread_signal, bool &stop_signal_called, const std::chrono::time_point<std::chrono::steady_clock> &stop_time, const float &rate);

void csdtest_tx_leaf_node(uhd::usrp::multi_usrp::sptr &usrp, uhd::tx_streamer::sptr &tx_stream, const size_t &N_zfc, const size_t &m_zfc, const float &csd_ch_pow, const uhd::time_spec_t &csd_detect_time, const float &min_ch_pow, const float &tx_wait);

uhd::time_spec_t csd_tx_ref_signal(uhd::usrp::multi_usrp::sptr &usrp, uhd::tx_streamer::sptr &tx_stream, const size_t &Ref_N_zfc, const size_t &Ref_m_zfc, const size_t &Ref_R_zfc, const size_t &pre_buffer_len, bool &stop_signal_called);

void csd_rx_test_signal(uhd::usrp::multi_usrp::sptr &usrp, uhd::rx_streamer::sptr &rx_stream, const size_t &test_signal_len, const uhd::time_spec_t &rx_time, const size_t &total_rx_samples, const std::string &file, bool &stop_signal_called);