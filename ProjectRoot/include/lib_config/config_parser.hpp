#ifndef CONFIG_PARSER
#define CONFIG_PARSER

#include "pch.hpp"

#include "log_macros.hpp"

// Class to parse config file and store values in a dictionary-like structure
class ConfigParser
{
public:
    // Method to parse the config file and store values in the map
    ConfigParser(const std::string &filename);

    // Method to retrieve value by variable name
    std::string getValue_str(const std::string &varName);
    size_t getValue_int(const std::string &varName);
    float getValue_float(const std::string &varName);

    // Method to add/modify a value
    void set_value(const std::string &varname, const std::string &varval, const std::string &vartype, const std::string &desc = "");

    // Print a list of configs in the parser
    void print_values();

private:
    // states to keep record of parsed information
    std::unordered_map<std::string, std::string> string_data;
    std::unordered_map<std::string, size_t> int_data;
    std::unordered_map<std::string, float> float_data;
    std::unordered_map<std::string, std::string> desc_data;

    // trim spaces from parsed info
    void trim(std::string &str);
};

#endif