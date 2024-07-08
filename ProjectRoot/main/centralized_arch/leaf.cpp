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

void producer_thread(USRP_class &usrp_obj, PeakDetectionClass &peakDet_obj, CycleStartDetector &csd_obj, ConfigParser &parser, std::atomic<bool> &csd_success_signal, std::string homeDirStr)
{
    // reception/producer params
    size_t max_rx_packet_size = usrp_obj.max_rx_packet_size;
    size_t round = 1;
    std::vector<std::complex<float>> buff(max_rx_packet_size);

    // post-csd transmission params
    WaveformGenerator wf_gen;
    size_t wf_len = parser.getValue_int("test-signal-len");
    size_t wf_reps = parser.getValue_int("test-tx-reps");
    size_t wf_gap = size_t(parser.getValue_float("tx-gap-millisec") / 1e3 * usrp_obj.tx_rate);
    size_t zfc_q = 41;
    size_t rand_seed = 0;
    float min_ch_scale = parser.getValue_float("min-e2e-amp");
    wf_gen.initialize(wf_gen.ZFC, wf_len, wf_reps, wf_gap, zfc_q, 1.0, rand_seed, true);

    auto producer_wrapper = [&csd_obj, &csd_success_signal](const std::vector<std::complex<float>> &samples, const size_t &sample_size, const uhd::time_spec_t &sample_time)
    {
        csd_obj.produce(samples, sample_size, sample_time);
        if (csd_success_signal)
            return true;
        else
            return false;
    };

    while (not stop_signal_called)
    {
        LOG_INFO_FMT("-------------- Round %1% ------------", round);

        // CycleStartDetector - producer loop
        auto rx_samples = usrp_obj.reception(stop_signal_called, max_rx_packet_size, 0, uhd::time_spec_t(0.0), false, producer_wrapper);

        LOG_INFO_FMT("------------------ Producer finished for round %1%! --------------", round);
        ++round;

        // Transmission after cyclestartdetector
        float est_ref_sig_amp = csd_obj.est_ref_sig_amp;
        float cfo = peakDet_obj.estimate_freq_offset();
        LOG_INFO_FMT("Estimated Frequency Offset = %1%.", cfo);
        usrp_obj.adjust_for_freq_offset(cfo);
        wf_gen.scale = min_ch_scale / est_ref_sig_amp;
        auto tx_samples = wf_gen.generate_waveform();
        uhd::time_spec_t tx_start_timer = csd_obj.csd_tx_start_timer;

        usrp_obj.transmission(tx_samples, tx_start_timer, stop_signal_called, false);

        csd_success_signal = false;
    }
}

void consumer_thread(CycleStartDetector &csd_obj, ConfigParser &parser, std::atomic<bool> &csd_success_signal)
{
    while (not stop_signal_called)
    {
        if (csd_obj.consume(csd_success_signal))
        {
            LOG_INFO("***Successful CSD!");
        }
    }
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

    auto my_producer_thread = thread_group.create_thread([=, &usrp_obj, &csd_obj, &peakDet_obj, &parser, &csd_success_signal]()
                                                         { producer_thread(usrp_obj, peakDet_obj, csd_obj, parser, csd_success_signal, homeDirStr); });

    uhd::set_thread_name(my_producer_thread, "producer_thread");

    // consumer thread
    auto my_consumer_thread = thread_group.create_thread([=, &csd_obj, &parser, &csd_success_signal]()
                                                         { consumer_thread(csd_obj, parser, csd_success_signal); });

    uhd::set_thread_name(my_consumer_thread, "consumer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    return EXIT_SUCCESS;
};