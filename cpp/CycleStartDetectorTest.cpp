#include <uhd/convert.hpp>
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
#include <thread>

namespace po = boost::program_options;
using namespace std::chrono_literals;

namespace
{
    constexpr auto CLOCK_TIMEOUT = 1000ms; // 1000mS timeout for external clock locking
} // namespace

using start_time_type = std::chrono::time_point<std::chrono::steady_clock>;

/***********************************************************************
 * Test result variables
 **********************************************************************/
std::atomic_ullong num_overruns{0};
std::atomic_ullong num_underruns{0};
std::atomic_ullong num_rx_samps{0};
std::atomic_ullong num_tx_samps{0};
std::atomic_ullong num_dropped_samps{0};
std::atomic_ullong num_seq_errors{0};
std::atomic_ullong num_seqrx_errors{0}; // "D"s
std::atomic_ullong num_late_commands{0};
std::atomic_ullong num_timeouts_rx{0};
std::atomic_ullong num_timeouts_tx{0};

inline auto time_delta(const start_time_type &ref_time)
{
    return std::chrono::steady_clock::now() - ref_time;
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

template <typename samp_type> // inserts type of samples generated by UHD rx_streamer
class CycleStartDetector
{
private:
    static_assert(std::is_same<samp_type, std::complex<double>>::value ||
                      std::is_same<samp_type, std::complex<float>>::value ||
                      std::is_same<samp_type, std::complex<short>>::value,
                  "Unsupported samp_type. Supported type = (std::complex<double>, std::complex<float>, or std::complex<short>).");

    std::vector<samp_type> samples_buffer; // Vector of vectors to store samples
    std::vector<uhd::time_spec_t> timer;   // vector to store sample receive times
    std::vector<samp_type> zfc_seq;
    size_t capacity;
    size_t front;
    size_t rear;
    size_t num_produced;

    boost::mutex mtx;
    boost::condition_variable cv_producer;
    boost::condition_variable cv_consumer;

    // Function to check if correlation is positive
    static std::pair<bool, uhd::time_spec_t> correlation_operation(
        const std::vector<samp_type> &samples,
        const std::vector<uhd::time_spec_t> &sample_times)
    {

        std::vector<samp_type> result(samples.size() + zfc_seq.size() - 1, 0); // Initialize result vector

        // Perform cross-correlation
        for (size_t i = 0; i < samples.size(); ++i)
        {
            for (size_t j = 0; j < zfc_seq.size(); ++j)
            {
                result[i + j] += samples[i] * std::conj(zfc_seq[j]);
            }
        }

        return {true, sample_times.back()}; // Return true and time of last sample if all samples are zero
    }

    // Function to find peaks
    // Function to find peak value and index in a vector of floats
    std::pair<std : vector<float>, std : vector<size_t>> findPeaks(const std::vector<float> &vec)
    {
        // Check if the vector is empty
        if (vec.empty())
        {
            // Return a pair with peak value NaN and index 0
            return {{std::numeric_limits<float>::quiet_NaN()}, {0}};
        }

        // Find the iterator pointing to the maximum element in the vector
        auto max_it = std::max_element(vec.begin(), vec.end());

        // Calculate the index of the maximum element
        size_t peak_index = std::distance(vec.begin(), max_it);

        // Return a pair with peak value and its index
        return {*max_it, peak_index};
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

public:
    CycleStartDetector(size_t capacity, size_t min_num_samps, size_t N, size_t m) : samples_buffer(capacity), timer(capacity), capacity(capacity), front(0), rear(0), num_produced(0), zfc_seq(generateZadoffChuSequence(N, m)) {}

    void produce(const samp_type &sample, const uhd::time_spec_t &time)
    {
        boost::unique_lock<boost::mutex> lock(mtx);
        cv_producer.wait(lock, [this]
                         { return front != (rear + 1) % capacity; }); // Wait for space to produce
        samples_buffer[rear] = sample;
        timer[rear] = time;
        rear = (rear + 1) % capacity;
        ++num_produced;
        cv_consumer.notify_one(); // Notify consumer that new data is available
    }

    std::pair<std::vector<samp_type>, uhd::time_spec_t> consume(size_t min_num_samps)
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv_consumer.wait(lock, [this, min_num_samps]
                         { return num_produced >= min_num_samps; }); // Wait until minimum number of samples are produced
        std::vector<samp_type> samples;
        std::vector<std::chrono::time_point<std::chrono::steady_clock>> sample_times;
        for (size_t i = 0; i < min_num_samps; ++i)
        {
            samples.push_back(buffer[(front + i) % capacity]);
            sample_times.push_back(timer[(front + i) % capacity]); // Store the time corresponding to each sample
        }
        auto result = correlation_operation(samples, sample_times); // Run the operation function on the samples
        front = (front + min_num_samps - 1) % capacity;             // Update front pointer to remove consumed samples
        num_produced -= (min_num_samps - 1);                        // Update the number of produced samples
        cv_producer.notify_one();                                   // Notify producer that space is available
        if (result.first)
        {
            return {samples, result.second};
        }
        else
        {
            return {{}, {}};
        }
    }
};

//******************************************************************************************************************************
//* Cycle start detector -- Rx stream producing thread
//******************************************************************************************************************************
void csd_rx_stream_producer(
    uhd::usrp::multi_usrp::sptr usrp,       // USRP object
    const std::string &rx_cpu,              // floating point precision of streamed data, default "fc32"
    uhd::rx_streamer::sptr rx_stream,       // rx_streamer object
    size_t num_samples_burst,               // samples per burst of data
    const start_time_type &start_time,      // start time of receiving processes, default 0.0
    std::atomic<bool> &burst_timer_elapsed, // Shared variable to trigger
    bool elevate_priority,
    double rx_delay)
{
    if (elevate_priority)
    {
        uhd::set_thread_priority_safe();
    }

    // print pre-test summary
    auto time_stamp = NOW();
    auto rx_rate = usrp->get_rx_rate() / 1e6;
    auto num_channels = rx_stream->get_num_channels();
    std::cout << boost::format("[%s] Rx receive rate %f Msps on %u channels\n") % time_stamp % rx_rate % num_channels;

    // setup variables and allocate buffer
    uhd::rx_metadata_t md;

    std::vector<char> buff(num_samples_burst * uhd::convert::get_bytes_per_item(rx_cpu));
    std::vector<void *> buffs;
    for (size_t ch = 0; ch < rx_stream->get_num_channels(); ch++)
        buffs.push_back(&buff.front()); // same buffer for each channel
    bool had_an_overflow = false;
    uhd::time_spec_t last_time;
    const double rate = usrp->get_rx_rate();

    uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    cmd.num_samps = num_samples_burst;

    // Multiple channels must be time aligned, so a default delay is set if
    // not specified.
    cmd.time_spec = usrp->get_time_now() + uhd::time_spec_t(
                                               rx_delay == 0.0 ? 0.05 : rx_delay);
    // Streaming can only start immediately if there is a single channel
    // and the user has not requested a delay.
    cmd.stream_now = (rx_delay == 0.0 and buffs.size() == 1);
    rx_stream->issue_stream_cmd(cmd);

    const float burst_pkt_time =
        std::max<float>(0.100f, (2 * num_samples_burst / rate));
    float recv_timeout = burst_pkt_time + (rx_delay == 0.0 ? 0.05 : rx_delay);

    bool stop_called = false;

    while (true) // keep receiving until stopped by thread termination
    {
        if (burst_timer_elapsed and not stop_called) // stop receiving command
        {
            rx_stream->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
            stop_called = true;
        }

        try
        {
            num_rx_samps += rx_stream->recv(buffs, cmd.num_samps, md, recv_timeout) * rx_stream->get_num_channels();
            recv_timeout = burst_pkt_time;
        }
        catch (uhd::io_error &e)
        {
            std::cerr << "[" << NOW() << "] Caught an IO exception. " << std::endl;
            std::cerr << e.what() << std::endl;
            return;
        }

        // handle the error codes
        switch (md.error_code)
        {
        case uhd::rx_metadata_t::ERROR_CODE_NONE:
            if (had_an_overflow)
            {
                had_an_overflow = false;
                const long dropped_samps = (md.time_spec - last_time).to_ticks(rate);
                if (dropped_samps < 0)
                {
                    std::cerr << "[" << NOW()
                              << "] Timestamp after overrun recovery "
                                 "ahead of error timestamp! Unable to calculate "
                                 "number of dropped samples."
                                 "(Delta: "
                              << dropped_samps << " ticks)\n";
                }
                num_dropped_samps += std::max<long>(1, dropped_samps);
            }
            if ((burst_timer_elapsed or stop_called) and md.end_of_burst)
            {
                return;
            }
            break;

        // ERROR_CODE_OVERFLOW can indicate overflow or sequence error
        case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
            last_time = md.time_spec;
            had_an_overflow = true;
            // check out_of_sequence flag to see if it was a sequence error or
            // overflow
            if (!md.out_of_sequence)
            {
                num_overruns++;
            }
            else
            {
                num_seqrx_errors++;
                std::cerr << "[" << NOW() << "] Detected Rx sequence error."
                          << std::endl;
            }
            break;

        case uhd::rx_metadata_t::ERROR_CODE_LATE_COMMAND:
            std::cerr << "[" << NOW() << "] Receiver error: " << md.strerror()
                      << ", restart streaming..." << std::endl;
            num_late_commands++;
            // Radio core will be in the idle state. Issue stream command to restart
            // streaming.
            cmd.time_spec = usrp->get_time_now() + uhd::time_spec_t(0.05);
            cmd.stream_now = (buffs.size() == 1);
            rx_stream->issue_stream_cmd(cmd);
            break;

        case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
            if (burst_timer_elapsed)
            {
                return;
            }
            std::cerr << "[" << NOW() << "] Receiver error: " << md.strerror()
                      << ", continuing..." << std::endl;
            num_timeouts_rx++;
            break;

            // Otherwise, it's an error
        default:
            std::cerr << "[" << NOW() << "] Receiver error: " << md.strerror()
                      << std::endl;
            std::cerr << "[" << NOW() << "] Unexpected error on recv, continuing..."
                      << std::endl;
            break;
        }
    }
}

/***********************************************************************
 * Main code + dispatcher
 **********************************************************************/
int UHD_SAFE_MAIN(int argc, char *argv[])
{
    // variables to be set by po
    std::string args;
    std::string rx_subdev;
    std::string rx_stream_args;
    double rx_rate;
    std::string rx_otw;
    std::string rx_cpu;
    std::string channel_list, rx_channel_list, tx_channel_list;
    std::atomic<bool> burst_timer_elapsed(false);
    size_t overrun_threshold, underrun_threshold, drop_threshold, seq_threshold;
    double rx_delay;
    std::string priority;
    bool elevate_priority = false;

    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value(""), "single uhd Receiver device address args")
        // ("duration", po::value<double>(&duration)->default_value(10.0), "duration for the test in seconds")
        ("rx_subdev", po::value<std::string>(&rx_subdev), "specify the device subdev for RX")
        ("rx_stream_args", po::value<std::string>(&rx_stream_args)->default_value(""), "stream args for RX streamer")
        ("rx_rate", po::value<double>(&rx_rate)->default_value(1000000), "specify RX rate (sps)")
        ("rx_otw", po::value<std::string>(&rx_otw)->default_value("sc16"), "specify the over-the-wire sample mode for RX")
        ("rx_cpu", po::value<std::string>(&rx_cpu)->default_value("fc32"), "specify the host/cpu sample mode for RX")
        ("channels", po::value<std::string>(&channel_list)->default_value("0"), "which channel(s) to use (specify \"0\", \"1\", \"0,1\", etc)")
        ("rx_channels", po::value<std::string>(&rx_channel_list), "which RX channel(s) to use (specify \"0\", \"1\", \"0,1\", etc)")
        ("overrun-threshold", po::value<size_t>(&overrun_threshold),
         "Number of overruns (O) which will declare the node a failure.")
        ("underrun-threshold", po::value<size_t>(&underrun_threshold),
         "Number of underruns (U) which will declare the node a failure.")
        ("drop-threshold", po::value<size_t>(&drop_threshold),
         "Number of dropped packets (D) which will declare the system a failure.")
        ("seq-threshold", po::value<size_t>(&seq_threshold),
         "Number of dropped packets (D) which will declare the system a failure.")
        // NOTE: TX delay defaults to 0.25 seconds to allow the buffer on the device to fill completely
        ("rx_delay", po::value<double>(&rx_delay)->default_value(0.0), "delay before starting RX in seconds")
        ("priority", po::value<std::string>(&priority)->default_value("normal"), "thread priority (normal, high)")
        // ("multi_streamer", "Create a separate streamer per channel")
        //******* Following are specific to the OTAC application *********
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // print the help message
    if (vm.count("help"))
    {
        std::cout << boost::format("OTAC Cycle Start Detector Test %s") % desc << std::endl;
        return ~0;
    }

    if (priority == "high")
    {
        uhd::set_thread_priority_safe();
        elevate_priority = true;
    }

    // create a usrp device
    std::cout << std::endl;
    uhd::device_addrs_t device_addrs = uhd::device::find(args, uhd::device::USRP);
    start_time_type start_time(std::chrono::steady_clock::now());
    std::cout << boost::format("[%s] Creating the usrp device with: %s...") % NOW() % args
              << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    // always select the subdevice first, the channel mapping affects the other settings
    if (vm.count("rx_subdev"))
    {
        usrp->set_rx_subdev_spec(rx_subdev);
    }

    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;
    int num_mboards = usrp->get_num_mboards();

    boost::thread_group thread_group;

    // check that the device has sufficient RX and TX channels available
    std::vector<std::string> channel_strings;
    std::vector<size_t> rx_channel_nums;

    if (!vm.count("rx_channels"))
    {
        rx_channel_list = channel_list;
    }

    boost::split(channel_strings, rx_channel_list, boost::is_any_of("\"',"));
    for (size_t ch = 0; ch < channel_strings.size(); ch++)
    {
        size_t chan = std::stoul(channel_strings[ch]);
        if (chan >= usrp->get_rx_num_channels())
        {
            throw std::runtime_error("Invalid channel(s) specified.");
        }
        else
        {
            rx_channel_nums.push_back(std::stoul(channel_strings[ch]));
        }
    }

    std::cout << boost::format("[%s] Setting device timestamp to 0...") % NOW()
              << std::endl;
    usrp->set_time_now(0.0);

    // set USRP rx_rate
    usrp->set_rx_rate(rx_rate);

    // create a receive streamer
    uhd::stream_args_t stream_args(rx_cpu, rx_otw);
    stream_args.channels = rx_channel_nums;
    stream_args.args = uhd::device_addr_t(rx_stream_args);
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
    size_t num_samples_burst = rx_stream->get_max_num_samps();

    // create buffer to stream data

    auto rx_thread = thread_group.create_thread([=, &burst_timer_elapsed, &buffs]()
                                                { csd_rx_stream_producer(usrp,
                                                                         rx_cpu,
                                                                         rx_stream,
                                                                         num_samples_burst,
                                                                         buffs,
                                                                         start_time,
                                                                         burst_timer_elapsed,
                                                                         elevate_priority,
                                                                         rx_delay); });
    uhd::set_thread_name(rx_thread, "csd_rx_stream");

    return EXIT_SUCCESS;
}