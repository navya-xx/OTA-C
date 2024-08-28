#ifndef UTILITY_FUNCS
#define UTILITY_FUNCS

#include "pch.hpp"
#include "log_macros.hpp"
#include "json.hpp"

std::string currentDateTime();
std::string currentDateTimeFilename();
void append_value_with_timestamp(const std::string &filename, std::ofstream &outfile, std::string value);
void save_stream_to_file(const std::string &filename, std::ofstream &outfile, std::vector<samp_type> stream);
void save_timer_to_file(const std::string &filename, std::ofstream &outfile, std::vector<double> stream);
std::vector<samp_type> read_from_file(const std::string &filename);
float averageAbsoluteValue(const std::vector<samp_type> &vec, const float lower_bound = 0.0);
std::vector<double> unwrap(const std::vector<samp_type> &complexVector);
size_t rational_number_approximation(double a, double e = 1e-10, size_t max_iter = 100);
float generateRandomFloat(float a, float b);
std::string floatToStringWithPrecision(float value, int precision);
float calc_signal_power(const std::vector<samp_type> &signal, const size_t &start_index = 0, const size_t &length = 0);
float calc_signal_power(const std::deque<samp_type> &signal, const size_t &start_index = 0, const size_t &length = 0);
void update_device_config_cfo(const std::string &serial, const float &cfo);
float obtain_last_cfo(const std::string &serial);

#endif // UTILITY_FUNCS