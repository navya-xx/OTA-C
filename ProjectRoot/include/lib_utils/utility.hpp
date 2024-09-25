#ifndef UTILITY_FUNCS
#define UTILITY_FUNCS

#include "pch.hpp"
#include "log_macros.hpp"
#include <pwd.h>
#include <unistd.h>

std::string currentDateTime();
std::time_t convertStrToTime(const std::string &datetime);
std::string convertTimeToStr(const std::time_t &datetime, const std::string &format = "%Y%m%d_%H_%M_%S");
std::string currentDateTimeFilename();
void append_value_with_timestamp(const std::string &filename, std::ofstream &outfile, std::string value);
void save_stream_to_file(const std::string &filename, std::ofstream &outfile, std::vector<std::complex<float>> stream);
void save_timer_to_file(const std::string &filename, std::ofstream &outfile, std::vector<double> stream);
std::vector<std::complex<float>> read_from_file(const std::string &filename);
float meanAbsoluteValue(const std::vector<std::complex<float>> &vec, const float lower_bound = 0.0);
float meanSquareValue(const std::vector<std::complex<float>> &vec, const size_t &start_index, const size_t &end_index, const float lower_bound = 0.0);
std::vector<double> unwrap(const std::vector<std::complex<float>> &complexVector);
size_t rational_number_approximation(double a, double e = 1e-10, size_t max_iter = 100);
float generateRandomFloat(float a, float b);
std::string floatToStringWithPrecision(float value, int precision);
float findMaxAbsValue(const std::vector<std::complex<float>> &vec);
float calc_signal_power(const std::vector<std::complex<float>> &signal, const size_t &start_index = 0, const size_t &length = 0, const float &min_power = 0.0);
float calc_signal_power(const std::deque<std::complex<float>> &signal, const size_t &start_index = 0, const size_t &length = 0, const float &min_power = 0.0);
void update_device_config_cfo(const std::string &serial, const float &cfo);
float obtain_last_cfo(const std::string &serial);
std::string get_home_dir();
std::pair<float, float> find_closest_gain(const std::string &json_filename, const float &input_power_dbm, const float &input_freq);
float toDecibel(float value, bool isPower = true);
float fromDecibel(float dB, bool isPower = true);

bool devices_json_read_write(json &config_data, const bool &is_read = true);
bool saveDeviceConfig(const std::string &device_id, const std::string &config_type, const float &config_val);
bool saveDeviceConfig(const std::string &device_id, const std::string &config_type, const json &config_val);
bool readDeviceConfig(const std::string &device_id, const std::string &config_type, float &config_val);
bool readDeviceConfig(const std::string &device_id, const std::string &config_type, json &config_val);
bool listActiveDevices(std::vector<std::string> &device_ids);

void correct_cfo_tx(std::vector<std::complex<float>> &signal, float &scale, float &cfo, size_t &counter);

struct GainPower
{
    double gain;
    double power_dbm;
};

#endif // UTILITY_FUNCS