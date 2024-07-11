#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"
#include "usrp_class.hpp"
#include "waveforms.hpp"
#include "cyclestartdetector.hpp"

#define LOG_LEVEL LogLevel::DEBUG
static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

void producer_thread(USRP_class &usrp_obj, ConfigParser &parser, const std::string &homeDirStr, std::atomic<bool> &start_csd)
{
    /* Producer thread
    -   Transmit Reference signal for CycleStartDetector - set 'start_csd = false'
    -   Start reception of the data frame
    -   Notify consumer of new data
    -   Move back to step one when flag 'start_csd == true'
    */

    WaveformGenerator wf_gen = WaveformGenerator();
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, q_zfc, 1.0, 0, true);
    auto tx_waveform = wf_gen.generate_waveform();
    uhd::time_spec_t transmit_time = uhd::time_spec_t(0.0); // initially transmit immediately

    /*-------- Main producer loop --------*/
    while (not stop_signal_called)
    {
        if (start_csd)
        {
                }
    }
}

void consumer_thread(USRP_class &usrp_obj, ConfigParser &parser, const std::string &homeDirStr, std::atomic<bool> &start_csd)
{
    /* Consumer thread
    -
    */
}

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    /*------ Initialize ---------------*/
    const char *homeDir = std::getenv("HOME");
    std::string homeDirStr(homeDir);
    std::string projectDir = homeDirStr + "/OTA-C/ProjectRoot";
    std::string curr_time_str = currentDateTimeFilename();

    if (argc < 2)
        throw std::invalid_argument("ERROR : device address missing! Pass it as first argument to the function call.");

    std::string device_id = argv[1];

    /*----- LOG ------------------------*/
    std::string logFileName = projectDir + "/storage/logs/leaf_" + device_id + "_" + curr_time_str + ".log";
    Logger::getInstance().initialize(logFileName);
    Logger::getInstance().setLogLevel(LOG_LEVEL);

    /*------ Parse Config -------------*/
    ConfigParser parser(projectDir + "/main/centralized_arch/config.conf");
    parser.set_value("device-id", device_id, "str", "USRP device number");
    if (argc > 2)
    {
        size_t rand_seed = std::stoi(argv[2]);
        parser.set_value("rand-seed", std::to_string(rand_seed), "int", "Random seed selected by the leaf node");
    }
    parser.print_values();

    /*------- USRP setup --------------*/
    USRP_class usrp_obj(parser);

    usrp_obj.initialize();

    parser.set_value("max-rx-packet-size", std::to_string(usrp_obj.max_rx_packet_size), "int", "Max Rx packet size");

    /*------ Threads - Transmitter / Receiver --------*/

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    // setup thread_group
    boost::thread_group thread_group;

    std::atomic<bool> start_csd(true);

    /* Producer thread */
    /* -> Transmit CSD ref signal, and then, start receiving data and notify consumer */
    auto my_producer_thread = thread_group.create_thread([=, &usrp_obj, &parser, &start_csd]()
                                                         { producer_thread(usrp_obj, parser, homeDirStr, start_csd); });

    uhd::set_thread_name(my_producer_thread, "producer_thread");

    /* Consumer thread */
    /* -> Consume received data - Identify frame start, and process data frame */
    auto my_consumer_thread = thread_group.create_thread([=, &usrp_obj, &parser, &start_csd]()
                                                         { consumer_thread(usrp_obj, parser, homeDirStr, start_csd); });

    uhd::set_thread_name(my_consumer_thread, "consumer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    return EXIT_SUCCESS;
};