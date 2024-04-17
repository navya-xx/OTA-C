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

namespace po = boost::program_options;
using namespace std::chrono_literals;

using start_time_type = std::chrono::time_point<std::chrono::steady_clock>;

static bool stop_signal_called = false;
static bool DEBUG = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

/***********************************************************************
 * Test result variables
 **********************************************************************/
// std::atomic_ullong num_overruns{0};
// std::atomic_ullong num_underruns{0};
// std::atomic_ullong num_rx_samps{0};
// std::atomic_ullong num_tx_samps{0};
// std::atomic_ullong num_dropped_samps{0};
// std::atomic_ullong num_seq_errors{0};
// std::atomic_ullong num_seqrx_errors{0}; // "D"s
// std::atomic_ullong num_late_commands{0};
// std::atomic_ullong num_timeouts_rx{0};
// std::atomic_ullong num_timeouts_tx{0};

inline auto time_delta(const start_time_type &ref_time)
{
    return std::chrono::steady_clock::now() - ref_time;
}

// Function to convert uhd::time_spec_t to std::chrono::system_clock::time_point
start_time_type convert_to_system_clock(uhd::time_spec_t time_spec)
{
    // Convert uhd::time_spec_t to microseconds
    int64_t time_micro = static_cast<int64_t>(time_spec.get_full_secs()) * 1000000LL + static_cast<int64_t>(time_spec.get_frac_secs());
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

#define NOW() (time_delta_str(start_time))

float MAX_MEAN_RATIO_THRESHOLD = 100.0;

template <typename samp_type> // inserts type of samples generated by UHD rx_streamer
class CycleStartDetector      // class implementing cycle start detector using ZFC seq as reference
{
private:
    // check for compatible samp_types
    static_assert(std::is_same<samp_type, std::complex<double>>::value ||
                      std::is_same<samp_type, std::complex<float>>::value ||
                      std::is_same<samp_type, std::complex<short>>::value,
                  "Unsupported samp_type. Supported type = (std::complex<double>, std::complex<float>, or std::complex<short>).");

    std::vector<samp_type> samples_buffer; // Vector of vectors to store samples
    std::vector<uhd::time_spec_t> timer;   // vector to store sample receive times
    size_t N_zfc, m_zfc, R_zfc;
    size_t max_sample_size;
    uhd::time_spec_t sample_duration;
    size_t num_samp_consume;
    std::vector<float> abs_corr;
    size_t capacity;
    size_t front;
    size_t rear;
    size_t num_produced;

    boost::mutex mtx;
    boost::condition_variable cv_producer;
    boost::condition_variable cv_consumer;

public:
    CycleStartDetector(size_t capacity, size_t max_sample_size, uhd::time_spec_t sample_duration, size_t num_samp_consume, size_t N_zfc, size_t m_zfc, size_t R_zfc) : samples_buffer(capacity), timer(capacity), num_samp_consume(num_samp_consume), abs_corr(num_samp_consume), max_sample_size(max_sample_size), sample_duration(sample_duration), capacity(capacity), front(0), rear(0), num_produced(0), N_zfc(N_zfc), m_zfc(m_zfc), R_zfc(R_zfc) {}

    std::vector<samp_type> zfc_seq = generateZadoffChuSequence(N_zfc, m_zfc);

    // Function to check if correlation is positive
    std::pair<int, int> correlation_operation()
    {
        // std::vector<samp_type> result(samples.size()); // Initialize result vector
        float mean_corr = 0.0;
        float max_corr = 0.0;

        // Perform cross-correlation - "equal" size
        for (size_t i = 0; i < num_samp_consume; ++i)
        {
            samp_type corr;
            for (size_t j = 0; j < N_zfc; ++j)
            {
                corr += samples_buffer[(front + i) % capacity] * std::conj(zfc_seq[j]);
            }
            float abs_val = std::abs(corr);
            abs_corr[i] = abs_val;
            mean_corr += abs_val;
            if (max_corr < abs_val)
            {
                max_corr = abs_val;
            }
        }

        mean_corr = mean_corr / num_samp_consume;

        // check if max/mean > threshold => existence of ZFC seq
        float max_to_mean_ratio = max_corr / mean_corr;

        std::vector<size_t> peak_indices;

        if (max_to_mean_ratio > MAX_MEAN_RATIO_THRESHOLD)
        {
            // find peaks with this threshold
            std::cout << "Peak detected in correlated signal!" << std::endl;
            std::cout << "Max to mean ratio : " << max_to_mean_ratio << std::endl;
            peak_indices = findPeaks(max_corr * 0.7, 20);
        }
        else
        {
            if (DEBUG)
                std::cout << "No peak detected! Discard samples and continue..." << std::endl;
            return {0, 0};
        }

        if (peak_indices.size() == 0)
        {
            std::cout << "No peak detected!" << std::endl;
            return {0, 0};
        }
        else if (peak_indices.size() < 2) // we need at least two peaks to make a decision
        {
            std::cout << "Only one peak detected! Keep sampling..." << std::endl;
            return {-1, peak_indices.front()};
        }
        else
        {
            // count number of peaks
            int num_peaks = peak_indices.size();

            // check if last peak is too close to the end -> i.e., there might be more peaks in next set of data
            auto max_it = std::max_element(peak_indices.begin(), peak_indices.end());
            // auto min_it = std::min_element(peak_indices.begin(), peak_indices.end());
            // int peak_diff = (*max_it - *min_it) / (num_peaks - 1);

            std::cout << "Last peak:" << *max_it << ". Last sample :" << num_samp_consume - 1 << ". ZFC seq len: " << zfc_seq.size() << std::endl;

            // check if all peaks are detected
            if (num_peaks < R_zfc)
            {
                std::cout << "All peaks are not detected! We detected " << num_peaks
                          << " peaks out of a total of " << R_zfc << std::endl;
                if (num_samp_consume - *max_it < zfc_seq.size())
                {
                    std::cout << "More peaks are expected in next batch of samples!" << std::endl;
                    return {-1, peak_indices.front()};
                }
                else
                {
                    std::cout << "Detection ended without detecting all peaks! Wait for next round of reference signals!" << std::endl;
                    return {0, peak_indices.front()};
                }
            }
            else
            {
                std::cout << "All peaks are successfully detected! Total peaks " << R_zfc << std::endl;
            }

            // send last peak
            return {1, peak_indices.back()};
        }
    }

    // Function to find peaks > peak_threshold
    std::vector<size_t> findPeaks(const float &peak_threshold, const int &min_peak_sep = 100)
    {
        // Check if the vector is empty
        if (abs_corr.empty())
        {
            return {0};
        }

        // Calculate the index of the maximum element
        std::vector<size_t> peak_indices;
        for (int i = 0; i < num_samp_consume; i++)
        {
            if (abs_corr[i] > peak_threshold)
            {
                // check for min_peak_sep between adjacent peaks
                int last_peak = peak_indices.back();
                if (i - last_peak > min_peak_sep)
                {
                    peak_indices.push_back(i);
                }
            }
        }

        // Return a pair with peak value and its index
        return peak_indices;
    }

    // Function to generate Zadoff-Chu sequence
    std::vector<samp_type> generateZadoffChuSequence(size_t N, int m)
    {
        std::vector<samp_type> sequence(N);

        // Calculate sequence
        for (size_t n = 0; n < N; ++n)
        {
            auto phase = -M_PI * m * n * (n + 1) / N;
            sequence[n] = std::exp(samp_type(0, phase));
        }

        return sequence;
    }

    // Insert sample into cyclic buffer - Let it be consumed before capacity is full
    void produce(const std::vector<samp_type> &samples, const uhd::time_spec_t &time, std::ofstream &outfile)
    {
        boost::unique_lock<boost::mutex> lock(mtx);

        if (DEBUG)
        {
            std::string lock_condition = ((rear + max_sample_size) % capacity != front) ? "true" : "false";
            std::cout << "Produce -- front " << front << ", Rear " << rear << ", num_produced " << num_produced << ", Samples " << samples.size() << ", lock condition " << lock_condition << std::endl;
        }

        cv_producer.wait(lock, [this]
                         { return capacity - num_produced >= max_sample_size; }); // Wait for enough space to produce

        if (DEBUG)
            std::cout << "Produce -- wait skip" << std::endl;

        // save in file
        if (outfile.is_open())
        {
            outfile.write((const char *)&samples.front(), samples.size() * sizeof(samp_type));
        }
        // insert first timer
        uhd::time_spec_t next_time = time;
        for (const auto &sample : samples)
        {
            samples_buffer[rear] = sample;
            timer[rear] = next_time; // store absolute sample times

            rear = (rear + 1) % capacity;
            next_time += sample_duration;
        }

        num_produced += samples.size();

        if (DEBUG)
            std::cout << "Produce -- num_produced " << num_produced << std::endl;

        cv_consumer.notify_one(); // Notify consumer that new data is available
    }

    std::pair<bool, uhd::time_spec_t> consume()
    {
        boost::unique_lock<boost::mutex> lock(mtx);

        if (DEBUG)
        {
            std::string lock_condition = (num_produced >= num_samp_consume) ? "true" : "false";
            std::cout << "Consume -- front " << front << ", Rear " << rear << ", num_produced " << num_produced << ", lock condition " << lock_condition << std::endl;
        }

        cv_consumer.wait(lock, [this]
                         { return num_produced >= num_samp_consume; }); // Wait until minimum number of samples are produced

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

        size_t last_peak_id = 0;

        // front = (front + num_samp_consume - max_sample_size) % capacity;
        // num_produced -= (num_samp_consume - max_sample_size);

        if (result.first == 0)
        { // No peak found -> discard all samples
            front = (front + num_samp_consume - max_sample_size) % capacity;
            num_produced -= (num_samp_consume - max_sample_size);
        }
        else if (result.first == -1)
        { // some peak found, but more are coming!
            size_t first_peak = result.second;
            size_t remove_samps = std::max(static_cast<size_t>(0), first_peak - 2 * N_zfc);
            front = (front + remove_samps);
            num_produced -= remove_samps;
        }
        else if (result.first == 1)
        { // found all peaks!
            last_peak_id = result.second;
            num_produced = 0; // reset
            successful_detection = true;
        }

        // if (DEBUG)
        //     std::cout << "Consumed -- front " << front << ", rear " << rear << ", num_produced " << num_produced << std::endl;

        cv_producer.notify_one(); // Notify producer that space is available

        // return {false, uhd::time_spec_t(0.0)};

        if (successful_detection)
        {
            return {true, timer[(front + last_peak_id) % capacity]};
        }
        else
        {
            return {false, uhd::time_spec_t(0.0)};
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
    std::string args, type, ant, subdev, ref, wirefmt, file;
    size_t channel, spb, capacity_mul, max_sample_size, num_samp_consume, N_zfc, m_zfc, R_zfc;
    double rate, freq, gain, bw, total_time, setup_time, lo_offset;
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
        ("gain", po::value<double>(&gain)->default_value(60.0), "gain for the RF chain")
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
        ("capacity-mul", po::value<size_t>(&capacity_mul)->default_value(10), "Buffer capacity in terms of multiples of maximum number of samples received by USRP in one burst.")
        ("num-samp-consume", po::value<size_t>(&num_samp_consume)->default_value(0), "Number of samples to apply correlation operation at once.")
        ("N-zfc", po::value<size_t>(&N_zfc)->default_value(257), "ZFC seq length.")
        ("m-zfc", po::value<size_t>(&m_zfc)->default_value(31), "ZFC seq identifier (prime).")
        ("R-zfc", po::value<size_t>(&R_zfc)->default_value(5), "ZFC seq repetitions.")
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
        std::cout << boost::format("Actual RX Freq: %f MHz...") % (usrp->get_rx_freq(channel) / 1e6)
                  << std::endl
                  << std::endl;
    }

    // set the rf gain
    if (vm.count("gain"))
    {
        std::cout << boost::format("Setting RX Gain: %f dB...") % gain << std::endl;
        usrp->set_rx_gain(gain, channel);
        std::cout << boost::format("Actual RX Gain: %f dB...") % usrp->get_rx_gain(channel)
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
    }

    // set the antenna
    if (vm.count("ant"))
        usrp->set_rx_antenna(ant, channel);

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

    if (spb == 0)
        spb = max_sample_size;

    if (num_samp_consume == 0)
    {
        num_samp_consume = std::max(max_sample_size, N_zfc * R_zfc * 2);
    }

    // calculate sample duration
    sample_duration = uhd::time_spec_t(static_cast<double>(1 / rate));

    size_t capacity = capacity_mul * spb;

    // create class object -> buffer handler
    CycleStartDetector<std::complex<float>> csdbuffer(capacity, max_sample_size, sample_duration, num_samp_consume, N_zfc, m_zfc, R_zfc);

    std::cout << "capacity " << capacity << " max_sample_size " << max_sample_size << " num_samp_consume " << num_samp_consume << std::endl;

    // atomic bool to signal thread stop
    std::atomic<bool> stop_thread_signal(false);

    typedef std::complex<float> samp_type;

    // setup thread_group
    boost::thread_group thread_group;

    // Rx producer thread
    auto rx_producer_thread = thread_group.create_thread([=, &csdbuffer, &stop_thread_signal]()
                                                         {
        uhd::rx_metadata_t md;
        bool overflow_message = true;

        std::ofstream outfile;
        outfile.open(file.c_str(), std::ofstream::binary);

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
        float recv_timeout         = burst_pkt_time + 0.05;
        size_t num_rx_samps        = 0;
        
        // Run this loop until either time expired (if a duration was given), until
        // the requested number of samples were collected (if such a number was
        // given), or until Ctrl-C was pressed.
        auto last_receive_time = std::chrono::steady_clock::now();
        
        while (not stop_signal_called and not stop_thread_signal and (total_time == 0.0 or std::chrono::steady_clock::now() <= stop_time))
        {

            if(DEBUG) 
                std::cout << "Receiving after " << time_delta_str(last_receive_time) << std::endl;
            last_receive_time = std::chrono::steady_clock::now();

            try {
                num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, recv_timeout, false);
                recv_timeout = burst_pkt_time;
            } catch (uhd::io_error& e) {
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
            if(DEBUG) std::cout << "Received " << num_rx_samps << std::endl;

            // // save in file
            // if (outfile.is_open())
            // {
            //     outfile.write((const char *)&buff.front(), num_rx_samps * sizeof(samp_type));
            // }

            // pass on to the cycle start detector
            csdbuffer.produce(buff, md.time_spec, outfile);
        } // while loop end
        const auto actual_stop_time = std::chrono::steady_clock::now();

        std::cout << "Rx streaming stopping at " << time_delta_str(actual_stop_time) << std::endl;

        if (outfile.is_open())
        {
            outfile.close();
        }

        // issue stop streaming command
        stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
        rx_stream->issue_stream_cmd(stream_cmd); });

    uhd::set_thread_name(rx_producer_thread, "csd_rx_producer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // CSD consumer thread
    auto rx_consumer_thread = thread_group.create_thread([=, &csdbuffer, &stop_thread_signal]()
                                                         {
        // keep consuming
        const auto start_time = std::chrono::steady_clock::now();
        const auto stop_time = start_time + std::chrono::milliseconds(int64_t(1000 * total_time));
        std::pair<bool, uhd::time_spec_t> result;

        while (not stop_signal_called and not stop_thread_signal and (total_time == 0.0 or std::chrono::steady_clock::now() <= stop_time))
        {
            result = csdbuffer.consume();

            // check result
            if (result.first == true)
            {
                std::cout << "Successful detection! Closing thread..." << std::endl;
                // Display timer captured
                std::cout << "Synchronized at " << time_delta_str(convert_to_system_clock(result.second)) << std::endl;
                break;
            }
            else
            {
                if (DEBUG)
                    std::cout << "Continue consumer" << std::endl;
            }
        } });

    uhd::set_thread_name(rx_consumer_thread, "csd_rx_consumer_thread");

    std::this_thread::sleep_for(std::chrono::seconds(10));
    // interrupt and join the threads
    stop_signal_called = true;
    thread_group.join_all();

    std::cout << "[" << NOW() << "] Cycle start detector test complete." << std::endl
              << std::endl;

    return EXIT_SUCCESS;
}