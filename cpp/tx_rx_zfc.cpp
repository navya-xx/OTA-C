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

static bool DEBUG = false;
static double TX_WAIT_TIME_MICROSECS = 1e6;

static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

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
    size_t channel, spb, N_zfc, m_zfc, R_zfc, ref_gap, Rx_N_zfc;
    double rate, freq, gain, bw, total_time, setup_time, lo_offset, pnr_threshold;
    uhd::time_spec_t sample_duration;

    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value("serial=32C79C6"), "multi uhd device address args")
        ("file", po::value<std::string>(&file)->default_value("rx_sync_samples.dat"), "name of the file to write binary samples to")
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
        ("Tx-N-zfc", po::value<size_t>(&N_zfc)->default_value(79), "TX ZFC seq length.")
        ("Tx-m-zfc", po::value<size_t>(&m_zfc)->default_value(31), "TX ZFC seq identifier (prime).")
        ("Tx-R-zfc", po::value<size_t>(&R_zfc)->default_value(1), "TX ZFC seq repetitions.")
        ("Rx-N-zfc", po::value<size_t>(&Rx_N_zfc)->default_value(79), "Length of ZFC sequence to back from synced receivers.")
        ("start-at", po::value<std::string>(&start_at)->default_value(""), "Set start time from CPU clock in HH:MM format.")
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

        std::cout << "Wait is over! Starting REF signal streaming now..." << std::endl;
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
    // bool continue_on_bad_packet = vm.count("continue") > 0;

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
    {
        usrp->set_tx_subdev_spec(subdev);
        usrp->set_rx_subdev_spec(subdev);
    }

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
        std::cout << boost::format("Setting TX LO Offset: %f MHz...") % (lo_offset / 1e6)
                  << std::endl;
        uhd::tune_request_t tune_request(freq, lo_offset);
        if (vm.count("int-n"))
            tune_request.args = uhd::device_addr_t("mode_n=integer");
        usrp->set_rx_freq(tune_request, channel);
        std::cout << boost::format("Actual RX Freq: %f MHz...") % (usrp->get_rx_freq(channel) / 1e6)
                  << std::endl
                  << std::endl;
        usrp->set_tx_freq(tune_request, channel);
        std::cout << boost::format("Actual TX Freq: %f MHz...") % (usrp->get_rx_freq(channel) / 1e6)
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
        std::cout << boost::format("Setting TX Gain: %f dB...") % gain << std::endl;
        usrp->set_tx_gain(gain, channel);
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

    std::cout << boost::format("Setting device timestamp to 0...") << std::endl;
    usrp->set_time_now(0.0);

    std::this_thread::sleep_for(std::chrono::milliseconds(int64_t(1000 * setup_time)));

    // check Ref and LO Lock detect
    if (not vm.count("skip-lo"))
    {
        std::cout << "Getting here--> for LO lock" << std::endl;
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
    std::cout << "Current timer of USRP -- " << time_delta_str(convert_to_system_clock(usrp_now)) << std::endl;

    // ---------------------------------------------------------------------------------------
    // TX - send ZFC reference signal for Cycle start detection
    // create a transmit streamer
    // linearly map channels (index0 = channel0, index1 = channel1, ...)
    uhd::stream_args_t tx_stream_args("fc32", wirefmt);
    std::vector<size_t> channel_nums;
    channel_nums.push_back(channel);
    tx_stream_args.channels = channel_nums;
    uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(tx_stream_args);

    // pre-fill the buffer with the waveform
    auto zfc_seq = generateZadoffChuSequence(N_zfc, m_zfc);

    std::cout << "ZFC seq len " << N_zfc << ", identifier " << m_zfc << std::endl;

    // allocate a buffer which we re-use for each channel
    if (spb == 0)
    {
        // spb = tx_stream->get_max_num_samps() * 10;
        spb = N_zfc * R_zfc;
    }
    std::vector<std::complex<float>> buff(spb);
    std::vector<std::complex<float> *> buffs(channel_nums.size(), &buff.front());

    for (size_t n = 0; n < spb; n++)
    {
        buff[n] = zfc_seq[n % N_zfc];
    }

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    // Set up metadata. We start streaming a bit in the future
    // to allow MIMO operation:
    uhd::tx_metadata_t txmd;
    txmd.start_of_burst = true;
    txmd.end_of_burst = false;
    txmd.has_time_spec = true;
    txmd.time_spec = usrp->get_time_now() + uhd::time_spec_t(0.1);

    size_t total_num_samps = buff.size();

    // compute time_stamp of beginning of last ref seq
    sample_duration = uhd::time_spec_t(static_cast<double>(1 / rate));
    // align to the first sample of the last sequence sent
    double add_duration = sample_duration.get_real_secs() * (total_num_samps - N_zfc + 1);
    uhd::time_spec_t ref_time = txmd.time_spec + uhd::time_spec_t(add_duration);

    // send data until the signal handler gets called
    // or if we accumulate the number of samples specified (unless it's 0)
    uint64_t num_acc_samps = 0;
    while (true)
    {
        // Break on the end of duration or CTRL-C
        if (stop_signal_called)
        {
            break;
        }
        // Break when we've transimitted nsamps
        if (total_num_samps <= num_acc_samps)
        {
            break;
        }

        // send the entire contents of the buffer
        num_acc_samps += tx_stream->send(buffs, buff.size(), txmd);

        // fill the buffer with the waveform
        // for (size_t n = 0; n < buff.size(); n++)
        // {
        //     buff[n] = zfc_seq[n % N_zfc];
        // }

        txmd.start_of_burst = false;
        txmd.has_time_spec = false;
    }

    // send a mini EOB packet
    txmd.end_of_burst = true;
    tx_stream->send("", 0, txmd);

    // finished
    std::cout << "Total number of samples transmitted: " << num_acc_samps << std::endl;
    std::cout << std::endl
              << "Done!" << std::endl
              << std::endl;

    // ---------------------------------------------------------------------------------------
    // setup Rx streaming

    typedef std::complex<float> samp_type;

    // create a receive streamer
    uhd::stream_args_t rx_stream_args("fc32", wirefmt);
    rx_stream_args.channels = channel_nums;
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(rx_stream_args);

    const auto stop_time =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(int64_t(1000 * total_time));

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uhd::rx_metadata_t rxmd;
    uhd::stream_cmd_t rx_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    rx_stream_cmd.stream_now = false;
    // start time for Rx -- start a bit early
    double add_rx_duration = TX_WAIT_TIME_MICROSECS / 1e6 - (sample_duration.get_real_secs() * Rx_N_zfc * 10);
    rx_stream_cmd.time_spec = uhd::time_spec_t(ref_time) + uhd::time_spec_t(add_rx_duration);
    std::cout << "Current USRP timer : " << size_t(usrp->get_time_now().get_real_secs() * 1e6) << " microsecs";
    std::cout << " Requested Rx timer : " << size_t(rx_stream_cmd.time_spec.get_real_secs() * 1e6) << " microsecs" << std::endl;
    size_t num_total_samps = 0;
    size_t total_samples_capture = Rx_N_zfc * 20;
    size_t max_rx_samples = std::min(rx_stream->get_max_num_samps(), total_samples_capture);
    rx_stream_cmd.num_samps = total_samples_capture;
    rx_stream->issue_stream_cmd(rx_stream_cmd);

    std::ofstream outfile;
    outfile.open(file.c_str(), std::ofstream::binary);

    bool overflow_message = true;
    bool continue_on_bad_packet = true;

    std::vector<std::complex<float>> rx_buff(max_rx_samples);
    double timeout = (rx_stream_cmd.time_spec - usrp->get_time_now()).get_real_secs() + rx_buff.size() * sample_duration.get_real_secs() + 0.1;

    while (not stop_signal_called and (num_total_samps <= total_samples_capture) and (total_time == 0.0 or std::chrono::steady_clock::now() <= stop_time))
    {

        size_t num_rx_samps = rx_stream->recv(&rx_buff.front(), rx_buff.size(), rxmd, timeout, false);

        if (rxmd.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
        {
            std::cout << boost::format("Timeout while streaming") << std::endl;
            break;
        }
        if (rxmd.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW)
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
        if (rxmd.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
        {
            std::string error = str(boost::format("Receiver error: %s") % rxmd.strerror());
            if (continue_on_bad_packet)
            {
                std::cerr << error << std::endl;
                continue;
            }
            else
                throw std::runtime_error(error);
        }

        num_total_samps += num_rx_samps;

        if (outfile.is_open())
        {
            outfile.write((const char *)&rx_buff.front(), num_rx_samps * sizeof(samp_type));
        }
    }

    if (outfile.is_open())
    {
        outfile.close();
    }

    std::cout << "Total " << num_total_samps << " samples save in file " << file << std::endl;

    std::cout << "[" << NOW() << "] Cycle start detector test complete." << std::endl
              << std::endl;

    return EXIT_SUCCESS;
}