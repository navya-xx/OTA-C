#include "Utility.hpp"

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