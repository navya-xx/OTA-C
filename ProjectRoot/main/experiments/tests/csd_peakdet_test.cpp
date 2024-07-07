#include "pch.hpp"
#include "log_macros.hpp"
#include "cyclestartdetector.hpp"
#include "ConfigParser.hpp"
#include "Utility.hpp"

#define LOG_LEVEL LogLevel::DEBUG
static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

void producer_thread(const std::vector<std::complex<float>> &stream_data, PeakDetectionClass &peakDet_obj, CycleStartDetector &csd_obj, ConfigParser &parser, std::atomic<bool> &csd_success_signal, std::string homeDirStr)
{
    uhd::time_spec_t init_timer = uhd::time_spec_t(0.0);
    size_t packet_size = parser.getValue_int("max-rx-packet-size");
    double sampling_time = 1 / parser.getValue_float("rate");
    size_t packet_counter = 0;

    while (not csd_success_signal and not stop_signal_called)
    {
        std::vector<std::complex<float>> data_it(stream_data.begin() + packet_counter * packet_size, stream_data.begin() + (packet_counter + 1) * packet_size);
        uhd::time_spec_t curr_packet_time = init_timer + uhd::time_spec_t(packet_counter * packet_size * sampling_time);
        ++packet_counter;
        csd_obj.produce(data_it, packet_size, curr_packet_time);
    }

    LOG_INFO("Producer finished");
}

void consumer_thread(CycleStartDetector &csd_obj, ConfigParser &parser, std::atomic<bool> &csd_success_signal)
{
    while (not csd_success_signal and not stop_signal_called)
    {
        if (csd_obj.consume(csd_success_signal))
            LOG_INFO("***Successful CSD!");
    }
}

int main()
{
    /*------ Initialize ---------------*/
    const char *homeDir;
    if (std::getenv("HOME"))
        homeDir = std::getenv("HOME");
    else
        homeDir = "/home/nuc/";
    std::string homeDirStr(homeDir);
    std::string projectDir = homeDirStr + "/OTA-C/ProjectRoot";
    std::string curr_time_str = currentDateTimeFilename();

    /*----- LOG ------------------------*/
    std::string device_id = "32B172B_32C79C6"; // refer to info.text
    std::string logFileName = projectDir + "/storage/logs/leaf_" + device_id + "_" + curr_time_str + ".log";
    Logger::getInstance().initialize(logFileName);
    Logger::getInstance().setLogLevel(LOG_LEVEL);

    /*------ Parse Config -------------*/
    ConfigParser parser(projectDir + "/main/centralized_arch/config.conf");
    parser.print_values();

    /*------ Read data -------------*/
    std::string filename = projectDir + "/storage/csd_test_data/rx_saved_file_32C79C6_20240707_txP80.dat";
    auto rx_data = read_from_file(filename);

    /*------ Run CycleStartDetector -------------*/
    double rx_sample_duration_float = 1 / parser.getValue_float("rate");
    uhd::time_spec_t rx_sample_duration = uhd::time_spec_t(rx_sample_duration_float);
    float noise_level = 0.000304542; // refer to info.text
    PeakDetectionClass peakDet_obj(parser, noise_level);
    CycleStartDetector csd_obj(parser, rx_sample_duration, peakDet_obj);

    /*------ Threads - Consumer / Producer --------*/
    std::atomic<bool> csd_success_signal(false);

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    // setup thread_group
    boost::thread_group thread_group;

    auto my_producer_thread = thread_group.create_thread([=, &csd_obj, &peakDet_obj, &parser, &csd_success_signal]()
                                                         { producer_thread(rx_data, peakDet_obj, csd_obj, parser, csd_success_signal, homeDirStr); });

    uhd::set_thread_name(my_producer_thread, "producer_thread");

    // consumer thread
    auto my_consumer_thread = thread_group.create_thread([=, &csd_obj, &parser, &csd_success_signal]()
                                                         { consumer_thread(csd_obj, parser, csd_success_signal); });

    uhd::set_thread_name(my_consumer_thread, "consumer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    return EXIT_SUCCESS;
}