#include "utility.hpp"

std::mutex fileMutex;

std::string currentDateTime()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto now_tm = *std::localtime(&time_t_now);

    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string currentDateTimeFilename()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto now_tm = *std::localtime(&time_t_now);

    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y%m%d_%H_%M_%S");
    return oss.str();
}

// Function to convert a string with format "%Y%m%d_%H_%M_%S" to std::tm
std::time_t convertStrToTime(const std::string &datetime)
{
    std::tm tm = {};
    std::istringstream ss(datetime);
    std::string format = "%Y%m%d_%H_%M_%S";
    ss >> std::get_time(&tm, format.c_str());
    if (ss.fail())
        LOG_WARN_FMT("Failed to parse time string %1% with format %2%", datetime, format);
    return std::mktime(&tm);
}

// Function to convert a string with format "%Y%m%d_%H_%M_%S" to std::tm
std::string convertTimeToStr(const std::time_t &datetime, const std::string &format)
{
    std::tm time_tm = *std::localtime(&datetime);
    std::ostringstream oss;
    oss << std::put_time(&time_tm, format.c_str());
    if (oss.fail())
        LOG_WARN_FMT("Failed to parse time %1% to string with format %2%", datetime, format);
    return oss.str();
}

void append_value_with_timestamp(const std::string &filename, std::ofstream &outfile, std::string value)
{
    // Open the file in append mode (if not already open)
    if (!outfile.is_open())
    {
        outfile.open(filename, std::ios::app);
        if (!outfile.is_open())
        {
            LOG_WARN("Error: Could not open file for writing.");
            return;
        }
    }

    std::string curr_time = currentDateTime();

    outfile << curr_time << "\t" << value << "\n";
    outfile.close();
}

void save_stream_to_file(const std::string &filename, std::ofstream &outfile, std::vector<std::complex<float>> stream)
{
    // Open the file in append mode (if not already open)
    if (!outfile.is_open())
    {
        outfile.open(filename, std::ios::out | std::ios::binary | std::ios::app);
        if (!outfile.is_open())
        {
            LOG_WARN("Error: Could not open file for writing.");
            return;
        }
    }

    for (const auto &complex_value : stream)
    {
        float real_val = complex_value.real();
        float complex_val = complex_value.imag();
        outfile.write(reinterpret_cast<char *>(&real_val), sizeof(complex_value.real()));
        outfile.write(reinterpret_cast<char *>(&complex_val), sizeof(complex_value.imag()));
    }
}

void save_timer_to_file(const std::string &filename, std::ofstream &outfile, std::vector<double> stream)
{
    // Open the file in append mode (if not already open)
    if (!outfile.is_open())
    {
        outfile.open(filename, std::ios::out | std::ios::binary | std::ios::app);
        if (!outfile.is_open())
        {
            LOG_WARN("Error: Could not open file for writing.");
            return;
        }
    }

    for (const double &time : stream)
    {
        outfile.write(reinterpret_cast<const char *>(&time), sizeof(double));
    }
}

std::vector<std::complex<float>> read_from_file(const std::string &filename)
{
    std::vector<std::complex<float>> data;

    // Open the file in binary mode
    std::ifstream inputFile(filename, std::ios::binary);
    if (!inputFile)
    {
        LOG_ERROR_FMT("Error opening file: %1%", filename);
        return data; // Return an empty vector if file cannot be opened
    }

    // Get the size of the file
    inputFile.seekg(0, std::ios::end);
    std::streamsize fileSize = inputFile.tellg();
    inputFile.seekg(0, std::ios::beg);

    // Ensure the file size is a multiple of the size of std::complex<float>
    if (fileSize % sizeof(std::complex<float>) != 0)
    {
        LOG_ERROR("File size is not a multiple of std::complex<float>");
        return data;
    }

    // Calculate the number of complex numbers in the file
    size_t numElements = fileSize / sizeof(std::complex<float>);

    // Resize the vector to hold all complex numbers
    data.resize(numElements);

    // Read the data
    inputFile.read(reinterpret_cast<char *>(data.data()), fileSize);

    if (!inputFile)
    {
        LOG_ERROR_FMT("Error reading file: ", filename);
        return std::vector<std::complex<float>>(); // Return an empty vector on read error
    }

    return data;
}

std::vector<double> unwrap(const std::vector<std::complex<float>> &complexVector)
{
    const double pi = M_PI;
    const double two_pi = 2 * M_PI;

    // Calculate the initial phase (angles) of the complex numbers
    std::vector<double> phase(complexVector.size());
    std::transform(complexVector.begin(), complexVector.end(), phase.begin(), [](const std::complex<float> &c)
                   { return std::arg(c); });

    // Unwrap the phase
    for (size_t i = 1; i < phase.size(); ++i)
    {
        double delta = phase[i] - phase[i - 1];
        if (delta > pi)
        {
            phase[i] -= two_pi;
        }
        else if (delta < -pi)
        {
            phase[i] += two_pi;
        }
    }

    return phase;
}

size_t rational_number_approximation(double a, double e, size_t max_iter)
{
    size_t N = static_cast<size_t>(std::ceil(1.0 / (2 * e)));
    size_t m = static_cast<size_t>(std::round(a * N));
    size_t iter = 0;

    while ((std::abs(a - static_cast<double>(m / N)) >= e) & (iter < max_iter))
    {
        N++;
        m = static_cast<size_t>(std::round(a * N));
        iter++;
    }
    return N;
}

float generateRandomFloat(float a, float b)
{
    // Random number generator
    std::random_device rd;  // Seed generator
    std::mt19937 gen(rd()); // Mersenne Twister RNG

    // Create a distribution for the range [a, b]
    std::uniform_real_distribution<float> dis(a, b);

    // Generate and return the random number
    return dis(gen);
}

std::string floatToStringWithPrecision(float value, int precision)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

float findMaxAbsValue(const std::vector<std::complex<float>> &vec)
{
    float max_abs_value = 0.0f;

    for (const auto &num : vec)
    {
        float abs_value = std::abs(num); // std::abs() returns the magnitude of a complex number
        if (abs_value > max_abs_value)
        {
            max_abs_value = abs_value;
        }
    }

    return max_abs_value;
}

float calc_signal_power(const std::vector<std::complex<float>> &signal, const size_t &start_index, const size_t &length, const float &min_power)
{
    size_t L;
    if (length == 0)
        L = signal.size() - start_index;
    else
        L = length;

    auto sig_pow = meanSquareValue(signal, start_index, start_index + L, min_power);

    return sig_pow;
}

float calc_signal_power(const std::deque<std::complex<float>> &signal, const size_t &start_index, const size_t &length, const float &min_power)
{
    std::vector<std::complex<float>> vector(signal.begin(), signal.end());
    return calc_signal_power(vector, start_index, length, min_power);
}

float meanSquareValue(const std::vector<std::complex<float>> &vec, const size_t &start_index, const size_t &end_index, const float lower_bound)
{
    float sum = 0.0;
    for (size_t i = start_index; i < end_index; ++i)
    {
        float sqr_val = std::norm(vec[i]);
        if (lower_bound > 0.0 and sqr_val < lower_bound)
            continue;

        sum += sqr_val;
    }
    return sum / (end_index - start_index);
}

float meanAbsoluteValue(const std::vector<std::complex<float>> &vec, const float lower_bound)
{
    float sum = 0.0;
    float abs_val = 0.0;
    size_t counter = 0;
    for (const auto &num : vec)
    {
        abs_val = std::abs(num);
        if (lower_bound > 0.0 and abs_val < lower_bound)
            continue;

        sum += abs_val;
        counter++;
    }
    return counter == 0 ? 0.0 : sum / counter;
}

void update_device_config_cfo(const std::string &serial, const float &cfo)
{
    std::string homeDirStr = get_home_dir();
    std::string projectDir = homeDirStr + "/OTA-C/ProjectRoot";
    std::string file = projectDir + "/config/devices.json";
    std::ifstream inputFile(file);
    if (!inputFile.is_open())
    {
        LOG_WARN_FMT("Failed to open the file: %1%", file);
    }
    json device_config;
    inputFile >> device_config;
    inputFile.close();

    for (auto &entry : device_config["leaf-nodes"])
    {
        if (entry.contains("serial") && entry["serial"] == serial)
        {
            entry["parameters"]["last_CFO"] = cfo;
            break;
        }
    }

    std::ofstream outputFile(file);
    if (!outputFile.is_open())
    {
        LOG_WARN_FMT("Failed to open the file for writing: %1%", file);
    }

    outputFile << device_config.dump(4);
    outputFile.close();
}

float obtain_last_cfo(const std::string &serial)
{
    std::string homeDirStr = get_home_dir();
    std::string projectDir = homeDirStr + "/OTA-C/ProjectRoot";
    std::string file = projectDir + "/config/devices.json";
    std::ifstream inputFile(file);
    if (!inputFile.is_open())
    {
        LOG_WARN_FMT("Failed to open the file: %1%", file);
    }
    json device_config;
    inputFile >> device_config;
    inputFile.close();

    for (auto &entry : device_config["leaf-nodes"])
    {
        if (entry.contains("serial") && entry["serial"] == serial)
        {
            return entry["parameters"]["last_CFO"].get<float>();
        }
    }

    LOG_WARN("CFO not found!!");
    return 0.0;
}

std::string get_home_dir()
{
    const char *homeDir = std::getenv("HOME");

    if (!homeDir)
    {
        // If HOME is not set, use getpwuid to get the home directory of the current user
        struct passwd *pw = getpwuid(getuid());
        if (pw && pw->pw_dir)
        {
            homeDir = pw->pw_dir;
        }
        else
        {
            LOG_ERROR("Unable to determine the home directory.");
            return ""; // Exit with an error code
        }
    }

    std::string homeDirStr(homeDir);

    return homeDirStr;
}

// Function to find the closest gain value corresponding to the given power_dBm
std::pair<float, float> find_closest_gain(const std::string &json_filename, const float &input_power_dbm, const float &input_freq)
{
    // Open and read the JSON file
    std::ifstream file(json_filename);
    if (!file.is_open())
    {
        LOG_WARN_FMT("Could not open JSON file %1%.", json_filename);
        return {-100.0, -100.0};
    }

    // Parse the JSON file
    json j;
    file >> j;

    // Variables to store the closest gain and power_dBm values
    float closest_gain = -100.0;
    float closest_power_dbm = -100.0;
    float min_diff = std::numeric_limits<float>::max();
    const auto &temp_freq_map = j["temp_freq_map"][0];
    bool found_freq = false;

    // Iterate through the temp_freq_map in the JSON data
    for (const auto &freq_map : temp_freq_map["freqs"])
    {
        float freq = freq_map["freq"];
        if (std::abs(freq - input_freq) < 1e3)
        {
            found_freq = true;
            // Iterate through the powers array to find the closest power_dBm
            for (const auto &power_entry : freq_map["powers"])
            {
                float gain = power_entry["gain"];
                float power_dbm = power_entry["power_dbm"];

                // Calculate the absolute difference between the input and the current power_dBm
                float diff = std::abs(input_power_dbm - power_dbm);

                // Check if the current difference is the smallest
                if (diff < min_diff)
                {
                    min_diff = diff;
                    closest_gain = gain;
                    closest_power_dbm = power_dbm;
                }
            }
            break;
        }
    }

    if (!found_freq)
    {
        LOG_WARN_FMT("Calibration data for input frequency %1% not found!", input_freq);
    }

    // Return the closest gain and the corresponding power_dBm
    return {closest_gain, closest_power_dbm};
}

// Function to convert power/amplitude to decibels
float toDecibel(float value, bool isPower)
{
    if (isPower)
    {
        return 10.0 * log10(value); // for power ratio
    }
    else
    {
        return 20.0 * log10(value); // for amplitude/voltage ratio
    }
}

// Function to convert decibels back to power/amplitude ratio
float fromDecibel(float dB, bool isPower)
{
    if (isPower)
    {
        return pow(10.0, dB / 10.0); // converting back to power ratio
    }
    else
    {
        return pow(10.0, dB / 20.0); // converting back to amplitude/voltage ratio
    }
}

// read/write devices.json file
bool devices_json_read_write(json &config_data, const bool &is_read)
{
    std::lock_guard<std::mutex> lock(fileMutex);
    std::string homeDir = get_home_dir();
    std::string devices_json_file = homeDir + "/OTA-C/ProjectRoot/config/devices.json";

    if (is_read)
    {
        try
        {
            std::ifstream inFile(devices_json_file);
            config_data = json::parse(inFile);
            return true;
        }
        catch (json::exception &e)
        {
            LOG_WARN_FMT("JSON error: %1%", e.what());
            return false;
        }
    }
    else
    {
        std::ofstream outFile(devices_json_file);
        if (outFile.is_open())
        {
            outFile << config_data.dump(4);
            outFile.close();
            LOG_DEBUG("Config written to devices.json file.");
            return true;
        }
        LOG_WARN("Writing config to devices.json file failed!");
        return false;
    }
}

// Save config value in devices.json file
bool saveDeviceConfig(const std::string &device_id, const std::string &config_type, const float &config_val)
{
    json devices_json_data;
    bool read_success = devices_json_read_write(devices_json_data, true);
    if (not read_success)
    {
        LOG_WARN("Failed to read config file.");
        return false;
    }

    if (not devices_json_data.contains(device_id))
    {
        LOG_WARN_FMT("Device ID %1% not found in devices.json", device_id);
        return false;
    }

    json config_data = devices_json_data[device_id]["config"];
    if (not config_data.contains(config_type))
    {
        LOG_WARN_FMT("Config type %2% for device ID %1% not found in devices.json", device_id, config_type);
        return false;
    }

    // insert new data
    config_data[config_type] = config_val;
    devices_json_data[device_id]["config"] = config_data;
    bool write_success = devices_json_read_write(devices_json_data, false);
    if (write_success)
        return true;
    else
        return false;
}

bool saveDeviceConfig(const std::string &device_id, const std::string &config_type, const json &config_val)
{
    json devices_json_data;
    bool read_success = devices_json_read_write(devices_json_data, true);
    if (not read_success)
    {
        LOG_WARN("Failed to read config file.");
        return false;
    }

    if (not devices_json_data.contains(device_id))
    {
        LOG_WARN_FMT("Device ID %1% not found in devices.json", device_id);
        return false;
    }

    json config_data = devices_json_data[device_id]["config"];
    if (not config_data.contains(config_type))
    {
        LOG_WARN_FMT("Config type %2% for device ID %1% not found in devices.json", device_id, config_type);
        return false;
    }

    // insert new data
    config_data[config_type] = config_val;
    devices_json_data[device_id]["config"] = config_data;
    bool write_success = devices_json_read_write(devices_json_data, false);
    if (write_success)
        return true;
    else
        return false;
}

// Read data from devices.json file
bool readDeviceConfig(const std::string &device_id, const std::string &config_type, float &config_val)
{
    json devices_json_data;
    bool read_success = devices_json_read_write(devices_json_data, true);
    if (not read_success)
    {
        LOG_WARN("Failed to read config file.");
        return false;
    }

    if (not devices_json_data.contains(device_id))
    {
        LOG_WARN_FMT("Device ID %1% not found in devices.json", device_id);
        return false;
    }

    json config_data = devices_json_data[device_id]["config"];
    if (not config_data.contains(config_type))
    {
        LOG_WARN_FMT("Config type %2% for device ID %1% not found in devices.json", device_id, config_type);
        return false;
    }

    try
    {
        config_val = config_data[config_type].get<float>();
        return true;
    }
    catch (json::exception &e)
    {
        LOG_WARN_FMT("JSON error %1%", e.what());
        return false;
    }
}

bool readDeviceConfig(const std::string &device_id, const std::string &config_type, json &config_val)
{
    json devices_json_data;
    bool read_success = devices_json_read_write(devices_json_data, true);
    if (not read_success)
    {
        LOG_WARN("Failed to read config file.");
        return false;
    }

    if (not devices_json_data.contains(device_id))
    {
        LOG_WARN_FMT("Device ID %1% not found in devices.json", device_id);
        return false;
    }

    json config_data = devices_json_data[device_id]["config"];
    if (not config_data.contains(config_type))
    {
        LOG_WARN_FMT("Config type %2% for device ID %1% not found in devices.json", device_id, config_type);
        return false;
    }

    try
    {
        config_val = config_data[config_type];
        return true;
    }
    catch (json::exception &e)
    {
        LOG_WARN_FMT("JSON error %1%", e.what());
        return false;
    }
}

bool listActiveDevices(std::vector<std::string> &device_ids)
{
    json devices_json_data;
    bool read_success = devices_json_read_write(devices_json_data, true);
    if (not read_success)
    {
        LOG_WARN("Saving config data failed! Failed to read config file.");
        return false;
    }

    for (const auto &item : devices_json_data.items())
    {
        std::string device_id = item.key();
        if (device_id.compare(0, 2, "32") == 0)
        {
            json dev_conf = item.value();
            std::string dev_type = dev_conf["type"].get<std::string>();
            if ((dev_type == "leaf"))
                device_ids.emplace_back(device_id);
        }
    }
    return true;
}

void correct_cfo_tx(std::vector<std::complex<float>> &signal, float &scale, float &cfo, size_t &counter)
{
    for (auto &samp : signal)
    {
        if (cfo != 0.0)
            samp *= scale * std::complex<float>(std::cos(cfo * counter), std::sin(cfo * counter));
        else
            samp *= scale;
        counter++;
    }
}