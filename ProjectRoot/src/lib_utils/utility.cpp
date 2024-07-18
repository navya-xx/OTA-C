#include "utility.hpp"

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

    for (auto &time : stream)
    {
        outfile.write(reinterpret_cast<char *>(&time), sizeof(time));
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

float averageAbsoluteValue(const std::vector<std::complex<float>> &vec, const float threshold)
{
    float sum = 0.0;
    float abs_val = 0.0;
    for (const auto &num : vec)
    {
        abs_val = std::abs(num);
        if (threshold > 0.0 and abs_val > threshold)
            continue;

        sum += abs_val;
    }
    return vec.empty() ? 0.0 : sum / vec.size();
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

void remove_freq_offset(std::vector<std::complex<float>> &samples, const float &freq_offset, const float &phase_offset, const float &sampling_time)
{
    for (size_t i = 0; i < samples.size(); ++i)
    {
    }
}