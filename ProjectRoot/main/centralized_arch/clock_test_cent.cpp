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

    parser.set_value("storage-folder", projectDir + "/storage", "str", "Location of storage director");

    /*------- USRP setup --------------*/
    USRP_class usrp_obj(parser);

    usrp_obj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_obj.initialize();

    parser.set_value("max-rx-packet-size", std::to_string(usrp_obj.max_rx_packet_size), "int", "Max Rx packet size");

    parser.print_values();

    WaveformGenerator wf_gen = WaveformGenerator();
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    // size_t wf_pad = size_t(parser.getValue_int("Ref-padding-mul") * N_zfc);
    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, 0, q_zfc, 1.0, 0);
    const auto tx_waveform = wf_gen.generate_waveform();
    const std::vector<std::complex<float>> zero_waveform(reps_zfc * N_zfc);
    // add fixed gap between ref signals

    // transmit ZFC seq as reference for sync
    // std::vector<std::complex<float>> tx_samples;
    // tx_samples.insert(tx_samples.begin(), wf_pad, std::complex<float>(0.0, 0.0));
    // size_t num_samples_gap = size_t(time_gap * usrp_obj.tx_rate);
    // for (int i = 0; i < 10; ++i)
    // {
    //     tx_samples.insert(tx_samples.end(), tx_waveform.begin(), tx_waveform.end());
    //     tx_samples.insert(tx_samples.end(), num_samples_gap, std::complex<float>(0.0, 0.0));
    // }

    // tx_samples.insert(tx_samples.end(), wf_pad, std::complex<float>(0.0, 0.0));

    double time_gap = 0.5; // 100ms gap
    usrp_obj.transmission(zero_waveform, uhd::time_spec_t(0.0), stop_signal_called, false);
    uhd::time_spec_t transmit_time = usrp_obj.usrp->get_time_now() + uhd::time_spec_t(1.0);
    for (int i = 0; i < 10; ++i)
    {
        LOG_INFO_FMT("Transmit time = %.4f microsecs", transmit_time.get_real_secs() * 1e6);
        usrp_obj.transmission(tx_waveform, transmit_time, stop_signal_called, true);
        transmit_time = transmit_time + uhd::time_spec_t(time_gap);
    }
    usrp_obj.transmission(zero_waveform, uhd::time_spec_t(0.0), stop_signal_called, false);

    // Receive samples for a fixed duration and return
    // double rx_duration_secs = 5.0;
    // auto received_samples = usrp_obj.reception(stop_signal_called, 0, rx_duration_secs, uhd::time_spec_t(0.0), true);

    return EXIT_SUCCESS;
};