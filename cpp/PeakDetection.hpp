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
#include <array>
#include "ConfigParser.hpp"

class PeakDetectionClass
{
private:
    ConfigParser parser;

    size_t *peak_indices;
    float *peak_vals;
    uhd::time_spec_t *peak_times;

    size_t total_num_peaks;

    size_t ref_seq_len;
    float pnr_threshold, curr_pnr_threshold;
    float init_noise_level;

    size_t peak_det_tol;
    float max_peak_mul;
    size_t sync_with_peak_from_last;

    bool save_buffer_flag;

    void insertPeak(const float &peak_val, const uhd::time_spec_t &peak_time);
    void update_pnr_threshold();
    void updatePrevPeak(const float &peak_val, const uhd::time_spec_t &peak_time);
    void removeLastPeak();
    float get_max_peak_val();
    bool check_peaks();

public:
    PeakDetectionClass(ConfigParser &parser, const float &init_noise_level, bool save_buffer_flag);

    size_t peaks_count;
    int prev_peak_index;
    float prev_peak_val;
    int samples_from_first_peak;
    bool detection_flag;
    std::deque<float> save_buffer_float;
    std::deque<std::complex<float>> save_buffer_complex;
    bool is_save_buffer_complex;

    float noise_level;
    long int noise_counter;

    float *get_peak_vals();
    uhd::time_spec_t *get_peak_times();
    void print_peaks_data();

    void resetPeaks();

    bool process_corr(const float &abs_val, const uhd::time_spec_t &samp_time);

    bool next();

    void save_float_data_into_buffer(const float &sample);
    void save_complex_data_into_buffer(const std::complex<float> &sample);
    void save_data_to_file(const std::string &file);

    void updateNoiseLevel(const float &corr_val, const size_t &num_samps);

    float get_avg_ch_pow();
    uhd::time_spec_t get_sync_time();

    // ~PeakDetectionClass();
};