#include "utility_funcs.hpp"

// Function to generate Zadoff-Chu sequence
std::vector<std::complex<float>> generateZadoffChuSequence(size_t N, int m)
{
    std::vector<std::complex<float>> sequence(N);

    // Calculate sequence
    for (size_t n = 0; n < N; ++n)
    {
        float phase = -M_PI * m * n * (n + 1) / N;
        sequence[n] = std::exp(std::complex<float>(0, phase));
    }

    return sequence;
}

inline auto time_delta(const start_time_type &ref_time)
{
    return std::chrono::steady_clock::now() - ref_time;
}

// Function to convert uhd::time_spec_t to std::chrono::system_clock::time_point
start_time_type convert_to_system_clock(uhd::time_spec_t time_spec)
{
    // Convert uhd::time_spec_t to microseconds
    int64_t time_micro = static_cast<int64_t>(time_spec.get_real_secs() * 1e6);
    std::chrono::microseconds us(time_micro);
    // Create a time_point from microseconds
    return start_time_type(us);
}

inline std::string time_delta_str(const start_time_type &ref_time)
{
    const auto delta = time_delta(ref_time);
    const auto hours = std::chrono::duration_cast<std::chrono::hours>(delta);
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(delta - hours);
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(delta - hours - minutes);
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
        delta - hours - minutes - seconds);

    return str(boost::format("%02d:%02d:%02d.%06d") % hours.count() % minutes.count() % seconds.count() % nanoseconds.count());
}

std::chrono::steady_clock::duration convert_timestr_to_duration(const std::string &mytime)
{
    // setup time struct
    std::tm desiredTime = {};
    std::istringstream ss(mytime);
    ss >> std::get_time(&desiredTime, "%H:%M");
    if (ss.fail())
    {
        throw std::runtime_error("Invalid time format. Use HH:MM format.");
    }

    // Convert the current time to std::time_t
    std::time_t currentTime_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm current_tm = *std::localtime(&currentTime_t);

    // Set the desired time date to the current date
    desiredTime.tm_year = current_tm.tm_year;
    desiredTime.tm_mon = current_tm.tm_mon;
    desiredTime.tm_mday = current_tm.tm_mday;

    // Convert the desired time tm to std::time_t
    std::time_t desiredTime_t = std::mktime(&desiredTime);

    std::cout << "Desired time to start " << std::ctime(&desiredTime_t) << std::endl;
    std::cout << "Current time on clock " << std::ctime(&currentTime_t) << std::endl;

    // Calculate the duration until the desired time
    auto duration = std::chrono::steady_clock::duration(desiredTime_t - currentTime_t);

    if (duration.count() <= 0)
    {
        std::cerr << "The desired time has already passed." << std::endl;
        return static_cast<std::chrono::microseconds>(0);
    }
    else
    {
        return duration;
    }
}

void print_duration(std::chrono::steady_clock::duration &time_duration)
{

    std::cout << "Program runs for a duration of " << time_duration.count() << " secs" << std::endl;
    // // Extract hours, minutes, seconds, and milliseconds from the duration
    // auto hours = std::chrono::duration_cast<std::chrono::hours>(time_duration);
    // time_duration -= hours;
    // auto minutes = std::chrono::duration_cast<std::chrono::minutes>(time_duration);
    // time_duration -= minutes;
    // auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_duration);
    // time_duration -= seconds;
    // auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time_duration);

    // // Print the duration in the desired format
    // std::cout << "Wait duration is :" << std::endl;
    // std::cout << std::setfill('0') << std::setw(2) << hours.count() << ":"
    //           << std::setw(2) << minutes.count() << ":"
    //           << std::setw(2) << seconds.count() << "."
    //           << std::setw(3) << milliseconds.count() << std::endl;
}

po::variables_map parse_config_from_file(const std::string &file)
{
    po::variables_map vm;
    std::ifstream configFile(file);

    std::cout << "Reading configuration from 'leaf_config.conf'" << std::endl;

    if (!configFile.is_open())
    {
        throw std::runtime_error("Failed to find config file 'leaf_config.conf'.");
    }

    // skip first line
    // std::string firstLine;
    // std::getline(configFile, firstLine);

    std::string line;
    while (std::getline(configFile, line))
    {
        std::istringstream iss(line);
        std::string varName, defaultValue, varType;

        if (!(iss >> varName >> defaultValue >> varType))
        {
            std::cerr << "Error: Invalid line in config file '" << file << "': " << line << std::endl;
            continue; // Skip invalid lines
        }

        vm.emplace(varName, po::variable_value(po::value<std::string>()->default_value(defaultValue), ""));

        // if (varType == "int")
        // {
        //     int value = std::stoi(defaultValue);
        //     vm.emplace(varName, po::variable_value(po::value<int>()->default_value(value), 0));
        // }
        // else if (varType == "float")
        // {
        //     double value = std::stod(defaultValue);
        //     vm.emplace(varName, po::variable_value(po::value<float>()->default_value(value), 0.0));
        // }
        // else if (varType == "str")
        // {
        //     vm.emplace(varName, po::variable_value(po::value<std::string>()->default_value(defaultValue), ""));
        // }
        // else
        // {
        //     std::cerr << "Error: Unsupported variable type in config file '" << file << "': " << varType << std::endl;
        //     continue; // Skip unsupported types
        // }
    }

    configFile.close();

    std::cout << "Reading configuration -> Done!" << std::endl;

    return vm;
}

// Helper function to convert string to specified type
template <typename T>
T convertToType(const std::string &str)
{
    std::istringstream iss(str);
    T value;
    iss >> value;
    return value;
}

void ConfigParser::parse(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Unable to open file " << filename << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string varName, varValue, varType;

        if (iss >> varName >> varValue >> varType)
        {
            if (varType == "int")
                int_data[varName] = static_cast<int>(std::stoi(varValue));
            else if (varType == "float")
                float_data[varName] = static_cast<float>(std::stof(varValue));
            else if (varType == "str" || varType == "string")
                string_data[varName] = varValue;
            else
                std::cerr << "Error : Unable to determine the type of variable " << varName << "." << std::endl;
        }
    }

    file.close();
}

const std::string ConfigParser::getValue_str(const std::string &varName)
{
    auto it = string_data.find(varName);
    if (it != string_data.end())
    {
        return it->second;
    }
    else
    {
        std::cerr << "Error: Variable '" << varName << "' not found in config" << std::endl;
    }
    return std::string();
}

const size_t ConfigParser::getValue_int(const std::string &varName)
{
    auto it = int_data.find(varName);
    if (it != int_data.end())
    {
        return it->second;
    }
    else
    {
        std::cerr << "Error: Variable '" << varName << "' not found in config" << std::endl;
    }
    return size_t();
}

const float ConfigParser::getValue_float(const std::string &varName)
{
    auto it = float_data.find(varName);
    if (it != float_data.end())
    {
        return it->second;
    }
    else
    {
        std::cerr << "Error: Variable '" << varName << "' not found in config" << std::endl;
    }
    return float();
}

void ConfigParser::print_values()
{
    std::cout << "Config values:" << std::endl;
    for (const auto &pair : string_data)
    {
        std::cout << pair.first << " = " << pair.second << std::endl;
    }

    for (const auto &pair : int_data)
    {
        std::cout << pair.first << " = " << pair.second << std::endl;
    }

    for (const auto &pair : float_data)
    {
        std::cout << pair.first << " = " << pair.second << std::endl;
    }
}
