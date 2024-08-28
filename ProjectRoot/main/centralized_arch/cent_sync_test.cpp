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

void receive_thread(USRP_class &usrp_obj, ConfigParser &parser, std::string homeDirStr)
{
    std::string curr_datetime = currentDateTimeFilename();
    std::string filename = homeDirStr + "/OTA-C/ProjectRoot/storage/rx_saved_file_" + parser.getValue_str("device-id") + "_" + curr_datetime + ".dat";
    usrp_obj.rx_save_stream.open(filename, std::ios::out | std::ios::binary | std::ios::app);
    if (!usrp_obj.rx_save_stream.is_open())
    {
        LOG_WARN("Error: Could not open file for writing.");
        return;
    }
    auto received_samples = usrp_obj.reception(stop_signal_called, 0, 0.0, uhd::time_spec_t(0.0), true);
}

void transmit_thread(USRP_class &usrp_obj, ConfigParser &parser)
{
    int num_runs = int(parser.getValue_int("num-test-runs"));

    WaveformGenerator wf_gen = WaveformGenerator();
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    size_t wf_pad = size_t(parser.getValue_int("Ref-padding-mul") * N_zfc);

    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    // wf_gen.initialize(wf_gen.IMPULSE, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    const auto tx_waveform = wf_gen.generate_waveform();

    for (int i = 0; i < num_runs; ++i)
    {
        LOG_INFO_FMT("---------------- ROUND : %1% -----------------", i);
        // uhd::time_spec_t transmit_time = usrp_obj.usrp->get_time_now() + uhd::time_spec_t(1.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        usrp_obj.transmission(tx_waveform, uhd::time_spec_t(0.0), stop_signal_called, true);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    stop_signal_called = true;
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
    ConfigParser parser(projectDir + "/config/config.conf");
    parser.set_value("device-id", device_id, "str", "USRP device number");
    if (argc > 2)
    {
        int num_runs = std::stoi(argv[2]);
        parser.set_value("num-test-runs", std::to_string(num_runs), "int");
    }
    parser.set_value("storage-folder", projectDir + "/storage", "str", "Location of storage director");

    /*------- USRP setup --------------*/
    USRP_class usrp_obj(parser);

    usrp_obj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_obj.initialize();

    parser.set_value("max-rx-packet-size", std::to_string(usrp_obj.max_rx_packet_size), "int", "Max Rx packet size");

    parser.print_values();

    /*------- Threads  ----------------*/

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // setup thread_group
    boost::thread_group thread_group;

    auto my_producer_thread = thread_group.create_thread([=, &usrp_obj, &parser]()
                                                         { receive_thread(usrp_obj, parser, homeDirStr); });

    uhd::set_thread_name(my_producer_thread, "receive_thread");

    // consumer thread
    auto my_consumer_thread = thread_group.create_thread([=, &usrp_obj, &parser]()
                                                         { transmit_thread(usrp_obj, parser); });

    uhd::set_thread_name(my_consumer_thread, "transmit_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    return EXIT_SUCCESS;
};