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
#include <exception>

// Class to parse config file and store values in a dictionary-like structure
class ConfigParser
{
public:
    // Method to parse the config file and store values in the map
    ConfigParser(const std::string &filename);

    // Method to retrieve value by variable name
    const std::string getValue_str(const std::string &varName);
    const size_t getValue_int(const std::string &varName);
    const float getValue_float(const std::string &varName);
    void set_value(const std::string &varname, const std::string &varval, const std::string &vartype);
    void print_values();

private:
    std::unordered_map<std::string, std::string> string_data;
    std::unordered_map<std::string, size_t> int_data;
    std::unordered_map<std::string, float> float_data;
};