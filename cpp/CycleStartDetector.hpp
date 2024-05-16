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
#include <vector>
#include <limits>
#include "PeakDetection.hpp"
#include "ConfigParser.hpp"
#include "utility_funcs.hpp"

class CycleStartDetector
{
public:
    CycleStartDetector(ConfigParser &parser, const uhd::time_spec_t &rx_sample_duration, PeakDetectionClass &peak_det_obj);

    void produce(const std::vector<std::complex<float>> &samples, const size_t &samples_size, const uhd::time_spec_t &time);

    bool consume(const bool &csd_success_signal);

    void correlation_operation();

    void reset();

    uhd::time_spec_t get_wait_time(float tx_wait_microsec);

private:
    std::vector<std::complex<float>> samples_buffer;
    std::vector<uhd::time_spec_t> timer;
    uhd::time_spec_t prev_timer;

    ConfigParser parser;
    uhd::time_spec_t rx_sample_duration;
    PeakDetectionClass &peak_det_obj_ref;

    size_t N_zfc, m_zfc, R_zfc;
    size_t num_samp_corr;
    size_t capacity;

    size_t front;
    size_t rear;
    size_t num_produced;
    std::vector<std::complex<float>> zfc_seq;

    boost::mutex mtx;
    boost::condition_variable cv_producer;
    boost::condition_variable cv_consumer;
};