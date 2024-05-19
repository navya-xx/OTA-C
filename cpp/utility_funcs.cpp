#include "utility_funcs.hpp"

// Function to generate Zadoff-Chu sequence
std::vector<std::complex<float>> generateZadoffChuSequence(size_t N, int m, float scale)
{
    std::vector<std::complex<float>> sequence(N);

    // Calculate sequence
    for (size_t n = 0; n < N; ++n)
    {
        float phase = -M_PI * m * n * (n + 1) / N;
        sequence[n] = scale * std::exp(std::complex<float>(0, phase));
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
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_duration);
    std::cout << "Program runs for a duration of " << seconds.count() << " secs" << std::endl;
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

void save_stream_to_file(const std::string &filename, std::ofstream &outfile, std::vector<std::complex<float>> stream)
{
    // Open the file in append mode (if not already open)
    if (!outfile.is_open())
    {
        outfile.open(filename, std::ios::out | std::ios::binary | std::ios::app);
        if (!outfile.is_open())
        {
            std::cerr << "Error: Could not open file for writing." << std::endl;
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

    if (outfile.is_open())
    {
        outfile.close();
    }
}
// Function to generate a vector of complex random variables on the unit circle
std::vector<std::complex<float>> generateUnitCircleRandom(size_t size, float scale)
{
    // Seed for random number generation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 2 * M_PI); // Uniform distribution for phase

    // Vector to store complex numbers
    std::vector<std::complex<float>> complexVector;

    // Generate random phases and construct complex numbers
    for (int i = 0; i < size; ++i)
    {
        float phase = dis(gen);
        complexVector.emplace_back(std::polar(scale, phase)); // Construct complex number with unit magnitude and phase
    }

    return complexVector;
}

std::string currentDateTime()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto now_tm = *std::localtime(&time_t_now);

    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// Function to convert amplitude ratio to dB
float amplitudeToDb(float value)
{
    return 20.0f * std::log10(value);
}

// Function to convert power ratio to dB
float powerToDb(float value)
{
    return 10.0f * std::log10(value);
}

// Function to convert dB to amplitude ratio
float dbToAmplitude(float dB)
{
    return std::pow(10.0f, dB / 20.0f);
}

// Function to convert dB to power ratio
float dbToPower(float dB)
{
    return std::pow(10.0f, dB / 10.0f);
}

// Function to calculate path loss in dB
float calculatePathLoss(const float &distance, const float &frequency)
{
    // Constants
    const float speedOfLight = 3e8; // Speed of light in meters per second
    const float constantTerm = 20 * std::log10(4 * M_PI / speedOfLight);

    // Calculate the distance term
    float distanceTerm = 20 * std::log10(distance);

    // Calculate the frequency term
    float frequencyTerm = 20 * std::log10(frequency);

    // Calculate the total path loss
    float pathLoss = distanceTerm + frequencyTerm + constantTerm;

    return pathLoss;
}
