#ifndef UTILITY_FUNCS
#define UTILITY_FUNCS

#include "pch.hpp"
#include "log_macros.hpp"
#include "json.hpp"

std::string currentDateTime();
std::string currentDateTimeFilename();
void save_stream_to_file(const std::string &filename, std::ofstream &outfile, std::vector<std::complex<float>> stream);
void save_timer_to_file(const std::string &filename, std::ofstream &outfile, std::vector<double> stream);
std::vector<std::complex<float>> read_from_file(const std::string &filename);
float averageAbsoluteValue(const std::vector<std::complex<float>> &vec, const float threshold = 0.0);
std::vector<double> unwrap(const std::vector<std::complex<float>> &complexVector);
size_t rational_number_approximation(double a, double e = 1e-10, size_t max_iter = 100);
float generateRandomFloat(float a, float b);

#endif // UTILITY_FUNCS