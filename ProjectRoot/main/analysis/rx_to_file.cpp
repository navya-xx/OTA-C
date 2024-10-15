#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"
#include "usrp_class.hpp"

#define LOG_LEVEL LogLevel::DEBUG
static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    /*------ Initialize ---------------*/
    std::string homeDirStr = get_home_dir();
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
    parser.print_values();

    /*------- USRP setup --------------*/
    USRP_class usrp_classobj(parser);
    usrp_classobj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_classobj.initialize();

    /*-------- Receive data stream --------*/
    size_t num_samples = 0;
    if (argc > 2)
    {
        num_samples = std::stoi(argv[2]);
    }
    else
    {
        float duration = 10.0;
        num_samples = size_t(duration * usrp_classobj.rx_rate);
    }

    LOG_INFO_FMT("Receiving %1% samples...", num_samples);

    std::vector<std::complex<float>> rx_samples;
    uhd::time_spec_t rx_start_timer;
    usrp_classobj.receive_fixed_num_samps(stop_signal_called, num_samples, rx_samples, rx_start_timer);
    std::vector<double> rx_start_timer_vec;
    rx_start_timer_vec.emplace_back(rx_start_timer.get_real_secs());

    LOG_INFO("Saving received samples and start timer.");
    std::ofstream rx_save_stream, rx_save_timer;
    save_stream_to_file(projectDir + "/storage/rxdata_" + device_id + "_" + curr_time_str + ".dat", rx_save_stream, rx_samples);
    save_timer_to_file(projectDir + "/storage/rxtimer_" + device_id + "_" + curr_time_str + ".dat", rx_save_timer, rx_start_timer_vec);

    return EXIT_SUCCESS;
};