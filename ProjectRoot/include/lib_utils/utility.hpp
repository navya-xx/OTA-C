#ifndef UTILITY_FUNCS
#define UTILITY_FUNCS

#include "pch.hpp"
#include "log_macros.hpp"

std::string currentDateTime();
std::string currentDateTimeFilename();
void save_stream_to_file(const std::string &filename, std::ofstream &outfile, std::vector<std::complex<float>> stream);
void save_timer_to_file(const std::string &filename, std::ofstream &outfile, std::vector<uhd::time_spec_t> stream);
std::vector<std::complex<float>> read_from_file(const std::string &filename);
float averageAbsoluteValue(const std::vector<std::complex<float>> &vec, const float threshold = 0.0);
std::vector<double> unwrap(const std::vector<std::complex<float>> &complexVector);

#endif // UTILITY_FUNCS