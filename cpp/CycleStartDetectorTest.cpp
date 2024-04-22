#include <uhd/convert.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <atomic>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <csignal>
#include <fstream>
#include <thread>
#include <boost/date_time.hpp>
#include <iomanip>
#include <sstream>
#include <deque>

namespace po = boost::program_options;
using namespace std::chrono_literals;

using start_time_type = std::chrono::time_point<std::chrono::steady_clock>;

static bool stop_signal_called = false;
static bool DEBUG = false;
static size_t PEAK_DETECTION_PARAM = 10;
static double TX_WAIT_TIME_MICROSECS = 10e5;
static float MIN_CH_POW_EST = 0.01;

void sig_int_handler(int)
{
    stop_signal_called = true;
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

    std::cout << "Duration " << time_duration.count() << " Seconds" << std::endl;
    // Extract hours, minutes, seconds, and milliseconds from the duration
    auto hours = std::chrono::duration_cast<std::chrono::hours>(time_duration);
    time_duration -= hours;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(time_duration);
    time_duration -= minutes;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_duration);
    time_duration -= seconds;
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time_duration);

    // Print the duration in the desired format
    std::cout << "Wait duration is :" << std::endl;
    std::cout << std::setfill('0') << std::setw(2) << hours.count() << ":"
              << std::setw(2) << minutes.count() << ":"
              << std::setw(2) << seconds.count() << "."
              << std::setw(3) << milliseconds.count() << std::endl;
}

#define NOW() (time_delta_str(start_time))

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

template <typename samp_type> // inserts type of samples generated by UHD rx_streamer
class CycleStartDetector      // class implementing cycle start detector using ZFC seq as reference
{
private:
    // check for compatible samp_types
    static_assert(std::is_same<samp_type, std::complex<double>>::value ||
                      std::is_same<samp_type, std::complex<float>>::value ||
                      std::is_same<samp_type, std::complex<short>>::value,
                  "Unsupported samp_type. Supported type = (std::complex<double>, std::complex<float>, or std::complex<short>).");

    std::vector<samp_type> samples_buffer;    // Vector of vectors to store samples
    std::vector<uhd::time_spec_t> timer;      // vector to store sample receive times
    size_t N_zfc, m_zfc, R_zfc;               // ZFC seq variables
    uhd::time_spec_t sample_duration;         // duration of one sample
    size_t num_samp_corr;                     // num of samples to correlate at once
    size_t capacity;                          // total capacity of the buffer
    size_t front;                             // marker for front of buffer
    size_t rear;                              // marker for rear of buffer
    size_t num_produced;                      // record of total samples in buffer
    std::vector<size_t> peak_indices;         // vector of detected peak indices
    std::vector<float> peak_vals;             // values of corresponding peaks
    size_t peaks_count;                       // num of peaks detected
    float pnr_threshold;                      // Threshold to detect peak
    std::vector<uhd::time_spec_t> peak_times; // vector of corresponding times of the peaks
    float rx_noise_power;                     // record of noise power for determining peak
    std::deque<samp_type> save_buffer;        // Vector of samples to save for later analysis
    bool successful_detection;                // flag for successful detection

    boost::mutex mtx;
    boost::condition_variable cv_producer;
    boost::condition_variable cv_consumer;

public:
    CycleStartDetector(size_t capacity, uhd::time_spec_t sample_duration, size_t num_samp_corr, size_t N_zfc, size_t m_zfc, size_t R_zfc, float rx_noise_power, float pnr_threshold) : samples_buffer(capacity), save_buffer(N_zfc * (R_zfc + 2), samp_type(0.0, 0.0)), successful_detection(false), timer(capacity), num_samp_corr(num_samp_corr), sample_duration(sample_duration), capacity(capacity), front(0), rear(0), num_produced(0), N_zfc(N_zfc), m_zfc(m_zfc), R_zfc(R_zfc), peaks_count(0), pnr_threshold(pnr_threshold), peak_indices(R_zfc), peak_vals(R_zfc), peak_times(R_zfc), rx_noise_power(rx_noise_power) {}

    // create and store ZFC seq for correlation operation
    const std::vector<samp_type> zfc_seq = generateZadoffChuSequence(N_zfc, m_zfc);

    size_t save_counter = 0;

    // save data
    void save_complex_data_to_file(std::ofstream &outfile)
    {
        auto complexDeque = save_buffer;
        if (outfile.is_open()) // we save as csv for analysis in python
        {
            for (auto it = complexDeque.begin(); it != complexDeque.end(); ++it)
            {
                outfile << it->real() << "|" << it->imag();
                if (std::next(it) != complexDeque.end())
                {
                    outfile << ",";
                }
            }
            outfile.close();
        }
    }

    float curr_pnr_threshold = pnr_threshold;

    // Function to check if correlation with ZFC seq is positive
    int correlation_operation()
    {
        bool found_peak = false;
        bool first_peak = true;
        size_t last_peak = 0;
        float last_pnr_val = 0.0;
        size_t adjacent_spacing;

        // Perform cross-correlation - "equal" size
        for (size_t i = 0; i < num_samp_corr; ++i)
        {
            // save values for later processing
            save_buffer.pop_front();
            save_buffer.push_back(samples_buffer[(front + i) % capacity]);

            // compute correlation
            samp_type corr(0.0, 0.0);
            for (size_t j = 0; j < N_zfc; ++j)
            {
                corr += samples_buffer[(front + i + j) % capacity] * std::conj(zfc_seq[j]);
            }

            float abs_val = std::abs(corr) / N_zfc;
            float peak_to_noise_ratio = (abs_val / std::sqrt(rx_noise_power));

            if (peaks_count == R_zfc) // run for another N_zfc to save rest of the ref signal in save_buffer
            {
                last_peak = peak_indices[peaks_count - 1];
                if (i < last_peak)
                    adjacent_spacing = num_samp_corr - last_peak + i;
                else
                {
                    adjacent_spacing = i - last_peak;
                }
                if (adjacent_spacing > 2 * N_zfc)
                {
                    std::cout << "Breaking! " << adjacent_spacing << " after last peak." << std::endl;
                    successful_detection = true;
                    break;
                }
            }
            // else if (peaks_count == R_zfc - 1)
            // {
            //     last_peak = peak_indices[peaks_count - 1];
            //     if (i < last_peak)
            //         adjacent_spacing = num_samp_corr - last_peak + i;
            //     else
            //     {
            //         adjacent_spacing = i - last_peak;
            //     }
            //     // check if R_zfc - 1 peaks are already found, and next peek is somehow missed
            //     if (adjacent_spacing > 2 * N_zfc)
            //     {
            //         std::cout << "Only " << peaks_count << " peaks found. Extrapolating timer." << std::endl;
            //         peak_indices[peaks_count] = last_peak + N_zfc;
            //         peak_times[peaks_count] = timer[(front + last_peak + N_zfc) % capacity];
            //         peak_vals[peaks_count] = peak_vals[peaks_count - 1];
            //         std::cout << "Breaking after " << peaks_count << " peak and " << adjacent_spacing << " samples." << std::endl;
            //         ++peaks_count;
            //         successful_detection = true;
            //         break;
            //     }
            // }

            if (peak_to_noise_ratio > curr_pnr_threshold)
            {
                found_peak = true;
                std::cout << "PNR " << peak_to_noise_ratio << ", Threshold " << curr_pnr_threshold << ", at " << i << std::endl;

                if (peaks_count <= R_zfc) // fill in the first peak
                {
                    // Detect the first peak correctly. Rest should follow at N_zfc spacing.
                    if (first_peak and peaks_count < 1)
                    {
                        peak_indices[0] = i;
                        peak_vals[0] = abs_val;
                        peak_times[0] = timer[(front + i) % capacity];
                        peaks_count = 1;
                        first_peak = false;
                        curr_pnr_threshold = pnr_threshold;
                        std::cout << "Peak number " << peaks_count << " found at " << peak_indices[peaks_count - 1] << ". PNR : " << peak_to_noise_ratio << std::endl;
                        continue;
                    }
                    else // next peaks
                    {
                        last_peak = peak_indices[peaks_count - 1];
                        last_pnr_val = (peak_vals[peaks_count - 1] / std::sqrt(rx_noise_power));
                        auto max_peak_val_it = std::max_element(peak_vals.begin(), peak_vals.end());
                        curr_pnr_threshold = (*max_peak_val_it * 0.9 / std::sqrt(rx_noise_power));
                        // distance of current peak from last
                        if (i < last_peak)
                            adjacent_spacing = num_samp_corr - last_peak + i;
                        else
                        {
                            adjacent_spacing = i - last_peak;
                        }
                        std::cout << "Peaks diff : " << adjacent_spacing << std::endl;

                        // next peak is too far from the last
                        if (adjacent_spacing > N_zfc + PEAK_DETECTION_PARAM) // reset to this peak
                        {
                            // can't find next peak
                            peak_indices[0] = i;
                            peak_vals[0] = abs_val;
                            peak_times[0] = timer[(front + i) % capacity];
                            peaks_count = 1;
                            first_peak = false;
                            curr_pnr_threshold = pnr_threshold;
                            std::cout << "Reset peaks -> number " << peaks_count << " found at " << peak_indices[peaks_count - 1] << "! PNR : " << peak_to_noise_ratio << std::endl;
                        }
                        else if (adjacent_spacing < N_zfc - PEAK_DETECTION_PARAM) // a peak exists in close proximity to last
                        {
                            if (last_pnr_val < peak_to_noise_ratio) // check is this peak is higher than the previous
                            {
                                // update last peak
                                peak_indices[peaks_count - 1] = i;
                                peak_vals[peaks_count - 1] = abs_val;
                                peak_times[peaks_count - 1] = timer[(front + i) % capacity];
                                std::cout << "Updated peak number " << peaks_count << " found at " << peak_indices[peaks_count - 1] << "! PNR : " << peak_to_noise_ratio << std::endl;
                                auto max_peak_val_it = std::max_element(peak_vals.begin(), peak_vals.end());
                                curr_pnr_threshold = (*max_peak_val_it * 0.9 / std::sqrt(rx_noise_power));
                            } // otherwise ignore this peak and continue
                            else
                            {
                                std::cout << "Peak does not fall in N_zfc +/- 5. Skipping..." << std::endl;
                            }
                        }
                        else // peak found in correct location
                        {
                            if (peaks_count < R_zfc)
                            {
                                // save peak
                                peak_indices[peaks_count] = adjacent_spacing + last_peak;
                                peak_vals[peaks_count] = abs_val;
                                peak_times[peaks_count] = timer[(front + i) % capacity];
                                ++peaks_count;
                                std::cout << "Peak number " << peaks_count << " found at " << peak_indices[peaks_count - 1] << "! PNR : " << peak_to_noise_ratio << std::endl;
                                auto max_peak_val_it = std::max_element(peak_vals.begin(), peak_vals.end());
                                curr_pnr_threshold = (*max_peak_val_it * 0.9 / std::sqrt(rx_noise_power));
                            }
                            else
                            {
                                throw std::runtime_error("Another peak detected after last! This should not happen...");
                                // std::cout << "Another peak detected after last! This should not happen..." << std::endl;
                            }
                        }
                    }
                }
            }
        }

        if (successful_detection)
        {
            std::cout << "All peaks are successfully detected! Total peaks " << R_zfc << std::endl;
            // send last peak
            return 1;
        }
        // else if (peaks_count < R_zfc) // not all peaks are detected yet
        // {
        //     std::cout << "All peaks are not detected! We detected " << peaks_count
        //               << " peaks out of a total of " << R_zfc << std::endl;
        //     return {-1, peak_indices.front()};
        // }
        else if (not found_peak)
        {
            if (DEBUG)
                std::cout << "No peak detected!" << std::endl;
            // reset counters
            peaks_count = 0;
            return 0;
        }
        else
        {
            return 0;
        }
    }

    // Insert sample into cyclic buffer - Let it be consumed before capacity is full
    void produce(const std::vector<samp_type> &samples, const size_t &samples_size, const uhd::time_spec_t &time)
    {
        boost::unique_lock<boost::mutex> lock(mtx);

        if (DEBUG)
        {
            std::string lock_condition = (capacity - num_produced >= samples_size) ? "true" : "false";
            std::cout << "Produce -- front " << front << ", Rear " << rear << ", num_produced " << num_produced << ", Samples " << samples_size << ", lock condition " << lock_condition << std::endl;
        }

        cv_producer.wait(lock, [this, samples_size]
                         { return capacity - num_produced >= samples_size; }); // Wait for enough space to produce

        if (DEBUG)
            std::cout << "Produce -- wait skip" << std::endl;

        // insert first timer
        uhd::time_spec_t next_time = time;

        // insert samples into the buffer
        for (const auto &sample : samples)
        {
            samples_buffer[rear] = sample;
            timer[rear] = next_time; // store absolute sample times

            rear = (rear + 1) % capacity;
            next_time += sample_duration;
        }

        num_produced += samples_size;

        if (DEBUG)
            std::cout << "Produce -- num_produced " << num_produced << std::endl;

        cv_consumer.notify_one(); // Notify consumer that new data is available
    }

    std::tuple<bool, uhd::time_spec_t, float> consume()
    {
        boost::unique_lock<boost::mutex> lock(mtx);

        if (DEBUG)
        {
            std::string lock_condition = (num_produced >= num_samp_corr + N_zfc) ? "true" : "false";
            std::cout << "Consume -- front " << front << ", Rear " << rear << ", num_produced " << num_produced << ", lock condition " << lock_condition << std::endl;
        }

        cv_consumer.wait(lock, [this]
                         { return num_produced >= num_samp_corr + N_zfc; }); // Wait until minimum number of samples are produced

        // test -- consume and carry on without processing
        // front = (front + num_samp_consume) % capacity;
        // num_produced -= (num_samp_consume);

        bool successful_detection = false;

        // std::vector<samp_type> samples;
        // std::vector<uhd::time_spec_t> sample_times;
        // for (size_t i = 0; i < num_samp_consume; ++i)
        // {
        //     samples.push_back(samples_buffer[(front + i) % capacity]);
        //     sample_times.push_back(timer[(front + i) % capacity]); // Store the time corresponding to each sample
        // }

        auto result = correlation_operation(); // Run the operation function on the samples

        // front = (front + num_samp_consume - max_sample_size) % capacity;
        // num_produced -= (num_samp_consume - max_sample_size);

        if (result == 0)
        { // No peak found -> discard all samples except last N_zfc for possible partial capture
            front = (front + num_samp_corr - 1) % capacity;
            num_produced -= (num_samp_corr - 1);
        }
        else if (result == 1)
        { // found all peaks!
            successful_detection = true;
        }
        else
        {
            throw std::runtime_error("Wrong detection flag!");
        }

        // if (DEBUG)
        //     std::cout << "Consumed -- front " << front << ", rear " << rear << ", num_produced " << num_produced << std::endl;

        cv_producer.notify_one(); // Notify producer that space is available

        // return {false, uhd::time_spec_t(0.0)};

        if (successful_detection)
        {
            float ch_pow = 0.0;
            for (float peak_val : peak_vals)
            {
                ch_pow += peak_val;
            }
            ch_pow /= peak_vals.size();
            return {true, peak_times.back(), ch_pow};
        }
        else
        {
            return {false, uhd::time_spec_t(0.0), 0.0};
        }
    }
};

typedef std::function<uhd::sensor_value_t(const std::string &)> get_sensor_fn_t;

bool check_locked_sensor(std::vector<std::string> sensor_names,
                         const char *sensor_name,
                         get_sensor_fn_t get_sensor_fn,
                         double setup_time)
{
    if (std::find(sensor_names.begin(), sensor_names.end(), sensor_name) == sensor_names.end())
        return false;

    auto setup_timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(int64_t(setup_time * 1000));
    bool lock_detected = false;

    std::cout << boost::format("Waiting for \"%s\": ") % sensor_name;
    std::cout.flush();

    while (true)
    {
        if (lock_detected and (std::chrono::steady_clock::now() > setup_timeout))
        {
            std::cout << " locked." << std::endl;
            break;
        }
        if (get_sensor_fn(sensor_name).to_bool())
        {
            std::cout << "+";
            std::cout.flush();
            lock_detected = true;
        }
        else
        {
            if (std::chrono::steady_clock::now() > setup_timeout)
            {
                std::cout << std::endl;
                throw std::runtime_error(
                    str(boost::format(
                            "timed out waiting for consecutive locks on sensor \"%s\"") %
                        sensor_name));
            }
            std::cout << "_";
            std::cout.flush();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << std::endl;
    return true;
}

/***********************************************************************
 * Main code + dispatcher
 **********************************************************************/
int UHD_SAFE_MAIN(int argc, char *argv[])
{
    // variables to be set by po
    std::string args, type, ant, subdev, ref, wirefmt, file, start_at;
    size_t channel, spb, capacity_mul, max_sample_size, num_samp_corr, Rx_N_zfc, Tx_N_zfc, Rx_m_zfc, Tx_m_zfc, R_zfc;
    double rate, freq, rx_gain, tx_gain, bw, total_time, setup_time, lo_offset, pnr_threshold;
    uhd::time_spec_t sample_duration;

    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value("serial=32C79BE"), "multi uhd device address args")
        ("file", po::value<std::string>(&file)->default_value("usrp_samples.dat"), "name of the file to write binary samples to")
        ("type", po::value<std::string>(&type)->default_value("float"), "sample type: double, float, or short")
        ("duration", po::value<double>(&total_time)->default_value(0), "total number of seconds to receive")
        ("spb", po::value<size_t>(&spb)->default_value(0), "samples per buffer")
        ("rate", po::value<double>(&rate)->default_value(1e6), "rate of incoming samples")
        ("freq", po::value<double>(&freq)->default_value(3e9), "RF center frequency in Hz")
        ("lo-offset", po::value<double>(&lo_offset)->default_value(0.0),
            "Offset for frontend LO in Hz (optional)")
        ("rx-gain", po::value<double>(&rx_gain)->default_value(60.0), "RX gain for the RF chain")
        ("tx-gain", po::value<double>(&tx_gain)->default_value(60.0), "TX gain for the RF chain")
        ("ant", po::value<std::string>(&ant), "antenna selection")
        ("subdev", po::value<std::string>(&subdev), "subdevice specification")
        ("channel", po::value<size_t>(&channel)->default_value(0), "which channel to use")
        ("bw", po::value<double>(&bw)->default_value(1e6), "analog frontend filter bandwidth in Hz")
        ("ref", po::value<std::string>(&ref)->default_value("internal"), "reference source (internal, external, mimo)")
        ("wirefmt", po::value<std::string>(&wirefmt)->default_value("sc16"), "wire format (sc8, sc16 or s16)")
        ("setup", po::value<double>(&setup_time)->default_value(1.0), "seconds of setup time")
        // ("progress", "periodically display short-term bandwidth")
        // ("stats", "show average bandwidth on exit")
        // ("sizemap", "track packet size and display breakdown on exit")
        // ("null", "run without writing to file")
        ("continue", "don't abort on a bad packet")
        ("skip-lo", "skip checking LO lock status")
        // ("int-n", "tune USRP with integer-N tuning")
        // For cycle start detector program
        ("capacity-mul", po::value<size_t>(&capacity_mul)->default_value(5), "Buffer capacity in terms of multiples of maximum number of samples received by USRP in one burst.")
        ("num-samp-corr", po::value<size_t>(&num_samp_corr)->default_value(0), "Number of samples to apply correlation operation at once.")
        ("Rx-N-zfc", po::value<size_t>(&Rx_N_zfc)->default_value(257), "Rx ZFC seq length.")
        ("Tx-N-zfc", po::value<size_t>(&Tx_N_zfc)->default_value(79), "Tx ZFC seq length.")
        ("Rx-m-zfc", po::value<size_t>(&Rx_m_zfc)->default_value(31), "Rx ZFC seq identifier (prime).")
        ("Tx-m-zfc", po::value<size_t>(&Tx_m_zfc)->default_value(31), "Tx ZFC seq identifier (prime).")
        ("R-zfc", po::value<size_t>(&R_zfc)->default_value(5), "Rx ZFC seq repetitions.")
        ("start-at", po::value<std::string>(&start_at)->default_value(""), "Set start time from CPU clock in HH:MM format.")
        ("pnr-threshold", po::value<double>(&pnr_threshold)->default_value(5e2), "Peak to noise ratio threshold for detecting peaks.")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // print the help message
    if (vm.count("help"))
    {
        std::cout << boost::format("UHD Rx -- Cycle Start Detector %s") % desc << std::endl;
        std::cout << std::endl
                  << "This application streams data from a single channel of a USRP "
                     "device, and check the received symbols for existance of ZFC reference signal.\n"
                  << std::endl;
        return ~0;
    }

    // ---------------------------------------------------------------------------------------
    // make program wait for to match starting time with start_time

    if (start_at.compare("") != 0)
    {
        auto wait_duration = convert_timestr_to_duration(start_at);

        // print_duration(wait_duration);
        std::cout << "Duration " << wait_duration.count() << " Seconds" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(wait_duration.count()));

        std::cout << "Wait is over! Starting Rx streaming now..." << std::endl;
    }
    else
    {
        std::cout << "Variable start_at is not set." << std::endl;
    }
    // ---------------------------------------------------------------------------------------

    // bool bw_summary = vm.count("progress") > 0;
    // bool stats = vm.count("stats") > 0;
    // bool null = vm.count("null") > 0;
    // bool enable_size_map = vm.count("sizemap") > 0;
    bool continue_on_bad_packet = vm.count("continue") > 0;

    // create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the usrp device with: %s...") % args
              << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    start_time_type start_time(std::chrono::steady_clock::now());

    // Lock mboard clocks
    if (vm.count("ref"))
    {
        std::cout << "Setting clock source as '" << ref << "'." << std::endl;
        usrp->set_clock_source(ref);
    }

    // always select the subdevice first, the channel mapping affects the other settings
    if (vm.count("subdev"))
        usrp->set_rx_subdev_spec(subdev);

    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    // set the sample rate
    if (rate <= 0.0)
    {
        std::cerr << "Please specify a valid sample rate" << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (rate / 1e6) << std::endl;
    usrp->set_rx_rate(rate, channel);
    std::cout << boost::format("Actual RX Rate: %f Msps...") % (usrp->get_rx_rate(channel) / 1e6)
              << std::endl
              << std::endl;

    std::cout << boost::format("Setting TX Rate: %f Msps...") % (rate / 1e6) << std::endl;
    usrp->set_tx_rate(rate);
    std::cout << boost::format("Actual TX Rate: %f Msps...") % (usrp->get_tx_rate() / 1e6)
              << std::endl
              << std::endl;

    // set the center frequency
    if (vm.count("freq"))
    { // with default of 0.0 this will always be true
        std::cout << boost::format("Setting RX Freq: %f MHz...") % (freq / 1e6)
                  << std::endl;
        std::cout << boost::format("Setting RX LO Offset: %f MHz...") % (lo_offset / 1e6)
                  << std::endl;
        uhd::tune_request_t tune_request(freq, lo_offset);
        if (vm.count("int-n"))
            tune_request.args = uhd::device_addr_t("mode_n=integer");
        usrp->set_rx_freq(tune_request, channel);
        usrp->set_tx_freq(tune_request, channel);
        std::cout << boost::format("Actual RX Freq: %f MHz...") % (usrp->get_rx_freq(channel) / 1e6)
                  << std::endl
                  << std::endl;
        std::cout << boost::format("Actual TX Freq: %f MHz...") % (usrp->get_tx_freq(channel) / 1e6)
                  << std::endl
                  << std::endl;
    }

    // set the rf gain
    if (vm.count("rx-gain"))
    {
        std::cout << boost::format("Setting RX Gain: %f dB...") % rx_gain << std::endl;
        usrp->set_rx_gain(rx_gain, channel);
        std::cout << boost::format("Actual RX Gain: %f dB...") % usrp->get_rx_gain(channel)
                  << std::endl
                  << std::endl;
    }

    if (vm.count("tx-gain"))
    {
        std::cout << boost::format("Setting TX Gain: %f dB...") % (tx_gain) << std::endl;
        usrp->set_tx_gain((tx_gain), channel);
        std::cout << boost::format("Actual TX Gain: %f dB...") % usrp->get_tx_gain(channel)
                  << std::endl
                  << std::endl;
    }

    // set the IF filter bandwidth
    if (vm.count("bw"))
    {
        std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % (bw / 1e6)
                  << std::endl;
        usrp->set_rx_bandwidth(bw, channel);
        std::cout << boost::format("Actual RX Bandwidth: %f MHz...") % (usrp->get_rx_bandwidth(channel) / 1e6)
                  << std::endl
                  << std::endl;

        std::cout << boost::format("Setting TX Bandwidth: %f MHz...") % (bw / 1e6)
                  << std::endl;
        usrp->set_tx_bandwidth(bw, channel);
        std::cout << boost::format("Actual TX Bandwidth: %f MHz...") % (usrp->get_tx_bandwidth(channel) / 1e6)
                  << std::endl
                  << std::endl;
    }

    // set the antenna
    if (vm.count("ant"))
    {
        usrp->set_rx_antenna(ant, channel);
        usrp->set_tx_antenna(ant, channel);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(int64_t(1000 * setup_time)));

    // check Ref and LO Lock detect
    if (not vm.count("skip-lo"))
    {
        check_locked_sensor(
            usrp->get_rx_sensor_names(channel),
            "lo_locked",
            [usrp, channel](const std::string &sensor_name)
            {
                return usrp->get_rx_sensor(sensor_name, channel);
            },
            setup_time);
        check_locked_sensor(
            usrp->get_tx_sensor_names(channel),
            "lo_locked",
            [usrp, channel](const std::string &sensor_name)
            {
                return usrp->get_tx_sensor(sensor_name, channel);
            },
            setup_time);
        if (ref == "mimo")
        {
            check_locked_sensor(
                usrp->get_mboard_sensor_names(0),
                "mimo_locked",
                [usrp](const std::string &sensor_name)
                {
                    return usrp->get_mboard_sensor(sensor_name);
                },
                setup_time);
        }
        if (ref == "external")
        {
            check_locked_sensor(
                usrp->get_mboard_sensor_names(0),
                "ref_locked",
                [usrp](const std::string &sensor_name)
                {
                    return usrp->get_mboard_sensor(sensor_name);
                },
                setup_time);
        }
    }

    uhd::time_spec_t usrp_now(usrp->get_time_now());
    std::cout << "Current timer of USRP -- " << static_cast<int64_t>(usrp_now.get_real_secs() * 1e6) << " microsecs." << std::endl;

    if (total_time == 0.0)
    {
        std::signal(SIGINT, &sig_int_handler);
        std::cout << "Press Ctrl + C to stop streaming..." << std::endl;
    }

    // create a receive streamer
    uhd::stream_args_t stream_args("fc32", wirefmt);
    std::vector<size_t> channel_nums;
    channel_nums.push_back(channel);
    stream_args.channels = channel_nums;
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

    max_sample_size = rx_stream->get_max_num_samps();

    typedef std::complex<float> samp_type;

    if (spb == 0)
        spb = max_sample_size;

    // estimate noise power
    // ---------------------------------------------------------------------------------------
    // setup streaming
    uhd::rx_metadata_t noise_md;
    uhd::stream_cmd_t noise_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
    noise_stream_cmd.num_samps = size_t(spb);
    noise_stream_cmd.stream_now = true;
    noise_stream_cmd.time_spec = uhd::time_spec_t();
    rx_stream->issue_stream_cmd(noise_stream_cmd);

    size_t noise_seq_len = spb * 5;

    std::vector<samp_type> noise_buff(noise_seq_len);

    try
    {
        rx_stream->recv(&noise_buff.front(), noise_seq_len, noise_md, 1.0, false);
    }
    catch (uhd::io_error &e)
    {
        std::cerr << "[" << NOW() << "] Caught an IO exception. " << std::endl;
        std::cerr << e.what() << std::endl;
    }

    float noise_power = 0.0;
    for (int i = 0; i < noise_buff.size(); i++)
    {
        noise_power += std::pow(std::abs(noise_buff[i]), 2);
    }
    noise_power /= noise_buff.size();

    std::cout << "Noise power " << noise_power << std::endl;

    // ---------------------------------------------------------------------------------------

    // calculate sample duration
    sample_duration = uhd::time_spec_t(static_cast<double>(1 / rate));

    if (num_samp_corr == 0)
    {
        num_samp_corr = std::max(max_sample_size, Rx_N_zfc * R_zfc * 2);
    }

    size_t capacity = capacity_mul * spb;

    // create class object -> buffer handler
    CycleStartDetector<std::complex<float>> csdbuffer(capacity, sample_duration, num_samp_corr, Rx_N_zfc, Rx_m_zfc, R_zfc, noise_power, pnr_threshold);

    std::cout << "capacity " << capacity << " max_sample_size " << max_sample_size << " num_samp_consume " << num_samp_corr << std::endl;

    // atomic bool to signal thread stop
    std::atomic<bool> stop_thread_signal(false);

    // setup thread_group
    boost::thread_group thread_group;

    // Rx producer thread
    auto rx_producer_thread = thread_group.create_thread([=, &csdbuffer, &stop_thread_signal]()
                                                         {
        uhd::rx_metadata_t md;
        bool overflow_message = true;

        // setup streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.num_samps = size_t(spb);
        stream_cmd.stream_now = true;
        stream_cmd.time_spec = uhd::time_spec_t();
        rx_stream->issue_stream_cmd(stream_cmd);

        const auto start_time = std::chrono::steady_clock::now();
        const auto stop_time =
            start_time + std::chrono::milliseconds(int64_t(1000 * total_time));

        std::cout << "Starting rx at " << time_delta_str(start_time_type(std::chrono::milliseconds(int64_t(0)))) << std::endl;

        std::vector<samp_type> buff(spb);

        const float burst_pkt_time = std::max<float>(0.100f, (2 * spb / rate));
        float recv_timeout = burst_pkt_time + 0.05;
        size_t num_rx_samps = 0;

        // Run this loop until either time expired (if a duration was given), until
        // the requested number of samples were collected (if such a number was
        // given), or until Ctrl-C was pressed.
        auto last_receive_time = std::chrono::steady_clock::now();

        while (not stop_signal_called and not stop_thread_signal)
        {
            if (total_time > 0 and std::chrono::steady_clock::now() > stop_time)
                stop_signal_called = true;

            if (DEBUG)
                std::cout << "Receiving after " << time_delta_str(last_receive_time) << std::endl;
            last_receive_time = std::chrono::steady_clock::now();

            try
            {
                num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, recv_timeout, false);
                recv_timeout = burst_pkt_time;
            }
            catch (uhd::io_error &e)
            {
                std::cerr << "[" << NOW() << "] Caught an IO exception. " << std::endl;
                std::cerr << e.what() << std::endl;
                return;
            }

            if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
            {
                std::cout << boost::format("Timeout while streaming") << std::endl;
                break;
            }
            if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW)
            {
                if (overflow_message)
                {
                    overflow_message = false;
                    std::cerr
                        << boost::format(
                               "Got an overflow indication. Please consider the following:\n"
                               "  Your write medium must sustain a rate of %fMB/s.\n"
                               "  Dropped samples will not be written to the file.\n"
                               "  Please modify this example for your purposes.\n"
                               "  This message will not appear again.\n") %
                               (usrp->get_rx_rate(channel) * sizeof(samp_type) / 1e6);
                }
                continue;
            }
            if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
            {
                std::string error = str(boost::format("Receiver error: %s") % md.strerror());
                if (continue_on_bad_packet)
                {
                    std::cerr << error << std::endl;
                    continue;
                }
                else
                    throw std::runtime_error(error);
            }

            // no error
            if (DEBUG)
                std::cout << "Received " << num_rx_samps << std::endl;

            // // save in file
            // if (outfile.is_open())
            // {
            //     outfile.write((const char *)&buff.front(), num_rx_samps * sizeof(samp_type));
            // }

            // pass on to the cycle start detector
            csdbuffer.produce(buff, num_rx_samps, md.time_spec);
        } // while loop end
        const auto actual_stop_time = std::chrono::steady_clock::now();

        std::cout << "Rx streaming stopping at " << time_delta_str(actual_stop_time) << std::endl;

        // issue stop streaming command
        stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
        rx_stream->issue_stream_cmd(stream_cmd); });

    uhd::set_thread_name(rx_producer_thread, "csd_rx_producer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    uhd::time_spec_t csd_detect_time;
    float csd_ch_pow;

    // CSD consumer thread
    auto rx_consumer_thread = thread_group.create_thread([=, &csdbuffer, &csd_detect_time, &csd_ch_pow, &stop_thread_signal]()
                                                         {
            // keep consuming
            const auto start_time = std::chrono::steady_clock::now();
            const auto stop_time = start_time + std::chrono::milliseconds(int64_t(1000 * total_time));
            
            std::tuple<bool, uhd::time_spec_t, float> result;

            while (not stop_signal_called and not stop_thread_signal)
            {
                if (total_time > 0 and std::chrono::steady_clock::now() > stop_time)
                    stop_signal_called = true;

                result = csdbuffer.consume();

                // check result
                if (std::get<0>(result) == true)
                {
                    std::cout << "Successful detection! Closing thread..." << std::endl;
                    // Display timer captured
                    std::cout << "Synchronization USRP timer : " << static_cast<int64_t>(std::get<1>(result).get_real_secs() * 1e6) << " microsecs." << std::endl;
                    stop_thread_signal = true;
                    break;
                }
                else
                {
                    if (DEBUG)
                        std::cout << "Continue consumer" << std::endl;
                }
            }

            // setting USRP timer to zero
            // usrp->set_time_now(0.0);
            // std::cout << "Set USRP timer to zero. Wait for 1 sec." << std::endl;
            // std::this_thread::sleep_for(std::chrono::seconds(1));
            uhd::time_spec_t usrp_now(usrp->get_time_now());
            // std::cout << "USRP timer after wait -- " << time_delta_str(convert_to_system_clock(usrp_now)) << std::endl; });
            int64_t time_micro = static_cast<int64_t>((usrp_now.get_real_secs() - std::get<1>(result).get_real_secs()) * 1e6);
            csd_detect_time = std::get<1>(result);
            csd_ch_pow = std::get<2>(result);
            std::cout << "USRP time elapsed after last peak detection -- " << time_micro << " micro secs" << std::endl; });

    uhd::set_thread_name(rx_consumer_thread, "csd_rx_consumer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    if (stop_signal_called)
        return EXIT_FAILURE;

    // save data
    size_t ext_from = args.find("=") + 1;
    size_t st_from = file.find_last_of("/") + 1;
    std::string rp_str = args.substr(ext_from) + "_";
    file.insert(st_from, rp_str);
    std::cout << "Saving to file '" << file << "'." << std::endl;
    std::ofstream outfile(file, std::ios::binary);
    csdbuffer.save_complex_data_to_file(outfile);

    // -----------------------------------------------------------------------------------------
    // start transmission

    std::cout << "Channel power estimated : " << csd_ch_pow << std::endl;

    std::vector<std::complex<float>> tx_zfc_seq = generateZadoffChuSequence(Tx_N_zfc, Tx_m_zfc);
    std::cout << "TX ZFC seq len " << Tx_N_zfc << ", identifier " << Tx_m_zfc << std::endl;

    // size_t num_max_tx_samps = tx_stream->get_max_num_samps();
    size_t num_max_tx_samps = Tx_N_zfc * 1;
    std::vector<std::complex<float>> tx_buff(num_max_tx_samps);

    for (size_t n = 0; n < tx_buff.size(); n++)
    {
        // tx_buff[n] = tx_zfc_seq[n % Tx_N_zfc] / csd_ch_pow * MIN_CH_POW_EST;
        tx_buff[n] = tx_zfc_seq[n % Tx_N_zfc];
    }

    // create a transmit streamer
    // linearly map channels (index0 = channel0, index1 = channel1, ...)
    uhd::stream_args_t tx_stream_args("fc32", "sc16");
    uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(tx_stream_args);

    const uhd::time_spec_t tx_timer = csd_detect_time + uhd::time_spec_t(TX_WAIT_TIME_MICROSECS / 1e6);
    uhd::tx_metadata_t txmd;
    txmd.has_time_spec = true;
    txmd.start_of_burst = false;
    txmd.end_of_burst = false;

    // int64_t time_diff = static_cast<int64_t>((tx_timer - usrp->get_time_now()).get_real_secs() * 1e6);
    // if (time_diff > 2000)
    // {
    //     std::cout << "Sleeping for " << time_diff - 2000 << " microsecs.";
    //     std::this_thread::sleep_for(std::chrono::microseconds(time_diff - 2000));
    //     time_diff = static_cast<int64_t>((tx_timer - usrp->get_time_now()).get_real_secs() * 1e6);
    //     std::cout << "Remaining time diff : " << time_diff << " microsecs." << std::endl;
    // }

    const double timeout = (tx_timer - usrp->get_time_now()).get_real_secs() + 0.1;

    txmd.time_spec = tx_timer;

    std::cout << "Current USRP timer : " << static_cast<int64_t>(usrp->get_time_now().get_real_secs() * 1e6) << ", desired timer : " << static_cast<int64_t>(txmd.time_spec.get_real_secs() * 1e6) << std::endl;

    size_t num_tx_samps = tx_stream->send(&tx_buff.front(), tx_buff.size(), txmd, timeout);

    // send a mini EOB packet
    txmd.has_time_spec = false;
    txmd.end_of_burst = true;
    tx_stream->send("", 0, txmd);

    std::cout << std::endl
              << "Waiting for async burst ACK... " << std::flush;
    uhd::async_metadata_t async_md;
    bool got_async_burst_ack = false;
    // loop through all messages for the ACK packet (may have underflow messages in queue)
    while (not got_async_burst_ack and tx_stream->recv_async_msg(async_md, timeout))
    {
        got_async_burst_ack =
            (async_md.event_code == uhd::async_metadata_t::EVENT_CODE_BURST_ACK);
    }
    std::cout << (got_async_burst_ack ? "success" : "fail") << std::endl;

    // std::cout << "Transmitted " << num_samp_tx << " samples." << std::endl;
    std::cout << "Transmitted " << num_tx_samps << " samples. Current USRP timer : " << static_cast<int64_t>(usrp->get_time_now().get_real_secs() * 1e6) << ", desired timer : " << static_cast<int64_t>(txmd.time_spec.get_real_secs() * 1e6) << std::endl;

    std::cout << "[" << NOW() << "] Cycle start detector test complete." << std::endl
              << std::endl;

    return EXIT_SUCCESS;
}