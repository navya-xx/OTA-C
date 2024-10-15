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
    size_t num_samples = 0, num_samples_saved = 0;
    if (argc > 2)
    {
        num_samples = std::stoi(argv[2]);
    }
    else
    {
        float duration = 10.0;
        num_samples = size_t(duration * usrp_classobj.rx_rate);
    }

    LOG_INFO("Implementing simple callback to save data to a deque vector.");
    std::deque<std::vector<std::complex<float>>> stream_deq(20);
    std::deque<uhd::time_spec_t> timer_deq(20);
    std::string filename = projectDir + "/storage/rxdata_" + device_id + "_" + curr_time_str + ".dat";
    std::ofstream rx_save_stream(filename, std::ios::out | std::ios::binary | std::ios::app);
    std::function save_stream_callback = [&stream_deq, &timer_deq, &rx_save_stream, &num_samples, &num_samples_saved, &filename](const std::vector<std::complex<float>> &rx_stream, const size_t &rx_stream_size, const uhd::time_spec_t &rx_timer)
    {
        // save_stream_to_file(filename, rx_save_stream, rx_stream);
        stream_deq.pop_front();
        stream_deq.push_back(std::move(rx_stream));
        timer_deq.pop_front();
        timer_deq.push_back(rx_timer);
        num_samples_saved += rx_stream_size;
        if (num_samples_saved < num_samples)
            return false;
        else
            return true;
    };

    usrp_classobj.receive_continuously_with_callback(stop_signal_called, save_stream_callback);

    rx_save_stream.close();

    LOG_INFO_FMT("Reception over! Total number of samples saved = %1%", num_samples_saved);

    return EXIT_SUCCESS;
};