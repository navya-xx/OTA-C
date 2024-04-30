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

template <typename samp_type>
class CycleStartDetector
{
public:
    CycleStartDetector(size_t capacity, uhd::time_spec_t sample_duration, size_t num_samp_corr, size_t N_zfc, size_t m_zfc, size_t R_zfc, float init_noise_level, float pnr_threshold);

    void produce(const std::vector<samp_type> &samples, const size_t &samples_size, const uhd::time_spec_t &time);
    bool consume();

    void save_complex_data_to_file(std::ofstream &outfile);
    int correlation_operation();

    std::vector<size_t> get_peakindices();
    std::vector<uhd::time_spec_t> get_timestamps();
    std::vector<float> get_peakvals();
    size_t get_peakscount();

    bool save_buffer_flag = true;

private:
    std::vector<samp_type> samples_buffer;
    std::vector<uhd::time_spec_t> timer;
    size_t N_zfc, m_zfc, R_zfc;
    uhd::time_spec_t sample_duration;
    size_t num_samp_corr;
    size_t capacity;
    size_t front;
    size_t rear;
    size_t num_produced;
    std::vector<size_t> peak_indices;
    std::vector<float> peak_vals;
    size_t peaks_count;
    float pnr_threshold, curr_pnr_threshold;
    std::vector<uhd::time_spec_t> peak_times;
    float init_noise_level;
    size_t noise_counter;
    std::deque<samp_type> save_buffer;
    bool successful_detection;

    boost::mutex mtx;
    boost::condition_variable cv_producer;
    boost::condition_variable cv_consumer;

    std::vector<samp_type> zfc_seq;
    size_t save_counter;
    size_t samples_from_first_peak;
};
