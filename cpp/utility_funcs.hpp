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
#include <map>
#include <unordered_map>
#include <random>
#include <cmath>
#include <numeric>

extern const bool DEBUG;

namespace po = boost::program_options;

using start_time_type = std::chrono::time_point<std::chrono::steady_clock>;

std::vector<std::complex<float>> generateZadoffChuSequence(size_t N, int m, float scale = 1.0);

inline auto time_delta(const start_time_type &ref_time);

start_time_type convert_to_system_clock(uhd::time_spec_t time_spec);

inline std::string time_delta_str(const start_time_type &ref_time);

std::chrono::steady_clock::duration convert_timestr_to_duration(const std::string &mytime);

void print_duration(std::chrono::steady_clock::duration &time_duration);

void save_stream_to_file(const std::string &filename, std::ofstream &outfile, std::vector<std::complex<float>> stream);

std::vector<std::complex<float>> generateUnitCircleRandom(size_t rand_seed, size_t size, float scale);

std::string currentDateTime();

float amplitudeToDb(float value);
float powerToDb(float value);
float dbToAmplitude(float dB);
float dbToPower(float dB);
float calculatePathLoss(const float &distance, const float &frequency);

void save_complex_data_to_file(const std::string &file, const std::vector<std::complex<float>> &save_buffer_complex, bool is_append = false);

std::vector<std::complex<float>> read_complex_data_from_file(const std::string &filename);

void save_float_data_to_file(const std::string &file, const std::vector<float> &save_buffer_float);

std::vector<double> unwrap(const std::vector<std::complex<float>> &complexVector);