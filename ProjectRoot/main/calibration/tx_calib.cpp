#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"
#include "usrp_class.hpp"
#include "waveforms.hpp"

#define LOG_LEVEL LogLevel::DEBUG
static bool stop_signal_called = false;
void sig_int_handler(int)
{
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
        throw std::invalid_argument("ERROR : device address is missing! Pass it as first argument to the function call.");

    std::string device_id = argv[1];
    // float tx_gain = std::stof(argv[2]);

    /*----- LOG ------------------------*/
    std::string logFileName = projectDir + "/storage/logs/leaf_" + device_id + "_" + curr_time_str + ".log";
    Logger::getInstance().initialize(logFileName);
    Logger::getInstance().setLogLevel(LOG_LEVEL);

    /*------ Parse Config -------------*/
    ConfigParser parser(projectDir + "/config/config.conf");
    parser.set_value("device-id", device_id, "str", "USRP device number");
    // parser.set_value("tx-gain", floatToStringWithPrecision(tx_gain, 1), "float", "USRP device number");

    parser.set_value("storage-folder", projectDir + "/storage", "str", "Location of storage director");

    /*------- USRP setup --------------*/
    USRP_class usrp_obj(parser);

    usrp_obj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_obj.initialize(false);

    parser.set_value("max-rx-packet-size", std::to_string(usrp_obj.max_rx_packet_size), "int", "Max Rx packet size");

    parser.print_values();

    WaveformGenerator wf_gen = WaveformGenerator();

    float tx_duration = 10.0; // seconds
    size_t num_samples = size_t(tx_duration / usrp_obj.tx_rate);
    size_t sin_len = 1000;
    size_t wf_reps = std::ceil(num_samples / sin_len);
    wf_gen.initialize(wf_gen.SINE, sin_len, wf_reps, 0, 0, 0, 1.0, 0);
    // wf_gen.initialize(wf_gen.IMPULSE, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    const auto tx_waveform = wf_gen.generate_waveform();

    /*------ Calibration setup --------*/

    for (float tx_gain_val = 50.0; tx_gain_val < 89.6; tx_gain_val = tx_gain_val + 1.0)
    {
        usrp_obj.set_tx_gain(tx_gain_val);

        LOG_INFO_FMT("---------------- STARTING TX GAIN : %1% -----------------", tx_gain_val);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        usrp_obj.transmission(tx_waveform, uhd::time_spec_t(0.0), stop_signal_called, true);

        LOG_INFO_FMT("FINISHED TX GAIN : %1%. Sleeping for 2 secs...", tx_gain_val);

        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }

    LOG_INFO("Calibration Ends!");

    return EXIT_SUCCESS;
};