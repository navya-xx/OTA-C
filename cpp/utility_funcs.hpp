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

extern const bool DEBUG;

namespace po = boost::program_options;

using start_time_type = std::chrono::time_point<std::chrono::steady_clock>;

std::vector<std::complex<float>> generateZadoffChuSequence(size_t N, int m, float scale = 1.0);

inline auto time_delta(const start_time_type &ref_time);

start_time_type convert_to_system_clock(uhd::time_spec_t time_spec);

inline std::string time_delta_str(const start_time_type &ref_time);

std::chrono::steady_clock::duration convert_timestr_to_duration(const std::string &mytime);

void print_duration(std::chrono::steady_clock::duration &time_duration);

po::variables_map parse_config_from_file(const std::string &file);

template <typename T>
T convertToType(const std::string &str);
