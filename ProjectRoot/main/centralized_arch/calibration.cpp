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

void producer_thread(USRP_class &usrp_obj, PeakDetectionClass &peakDet_obj, CycleStartDetector &csd_obj, ConfigParser &parser, std::atomic<bool> &csd_success_signal, std::string homeDirStr, const bool &is_cent, const size_t &max_num_rounds)
{
    // reception/producer params
    size_t max_rx_packet_size = usrp_obj.max_rx_packet_size;
    size_t round = 1;
    std::vector<std::complex<float>> buff(max_rx_packet_size);

    WaveformGenerator wf_gen = WaveformGenerator();
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    size_t wf_pad = size_t(parser.getValue_int("Ref-padding-mul") * N_zfc);
    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    // wf_gen.initialize(wf_gen.IMPULSE, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    auto tx_waveform = wf_gen.generate_waveform();

    std::string storage_dir = parser.getValue_str("storage-folder");
    std::string device_id = parser.getValue_str("device-id");
    std::string curr_time_str = currentDateTimeFilename();

    float rx_duration = is_cent ? 3.0 : 0.0; // fix duration for cent node

    // This function is called by the receiver as a callback everytime a frame is received
    auto producer_wrapper = [&csd_obj, &csd_success_signal](const std::vector<std::complex<float>> &samples, const size_t &sample_size, const uhd::time_spec_t &sample_time)
    {
        csd_obj.produce(samples, sample_size, sample_time, stop_signal_called);

        if (csd_success_signal)
            return true;
        else
            return false;
    };

    if (is_cent)
    { // start with transmission
        bool transmit_success = usrp_obj.transmission(tx_waveform, uhd::time_spec_t(0.0), stop_signal_called, false);
        if (!transmit_success)
            LOG_WARN("Transmission Unsuccessful!");
        else
            LOG_INFO("Transmission Sucessful!");

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    while (not stop_signal_called && round < max_num_rounds)
    {
        LOG_INFO_FMT("-------------- Round %1% ------------", round);

        // CycleStartDetector - producer loop
        // debug
        std::string curr_time_str = currentDateTimeFilename();

        std::string ref_datfile = storage_dir + "/logs/saved_ref_leaf_" + device_id + "_" + curr_time_str + ".dat";
        csd_obj.saved_ref_filename = ref_datfile;

        auto rx_samples = usrp_obj.reception(stop_signal_called, 0, rx_duration, uhd::time_spec_t(0.0), false, producer_wrapper);

        if (stop_signal_called)
            break;

        // LOG_INFO_FMT("Estimated Clock Drift = %.8f samples/sec.", csd_obj.estimated_sampling_rate_offset);
        // usrp_obj.adjust_for_freq_offset(csd_obj.estimated_sampling_rate_offset);
        // LOG_INFO_FMT("Corrected Clock Drift -> New sampling rate = %1% samples/sec.", usrp_obj.rx_rate);

        if (csd_success_signal)
        {
            LOG_INFO_FMT("------------------ Producer finished for round %1%! --------------", round);
            ++round;
        }
        else
        {
            LOG_INFO_FMT("No calibration signal received in Round %1%. Re-transmitting...", round);
        }

        // Transmission after cyclestartdetector
        uhd::time_spec_t tx_start_timer = csd_obj.csd_tx_start_timer;
        LOG_INFO_FMT("Current timer %1% and Tx start timer %2%.", usrp_obj.usrp->get_time_now().get_real_secs(), tx_start_timer.get_real_secs());

        // adjust for CFO
        if (csd_obj.cfo != 0.0)
        {
            int counter = 0;
            for (auto &samp : tx_waveform)
            {
                samp *= std::complex<float>(std::cos(csd_obj.cfo * counter), std::sin(csd_obj.cfo * counter));
                counter++;
            }
        }
        bool transmit_success = usrp_obj.transmission(tx_waveform, tx_start_timer, stop_signal_called, false);
        if (!transmit_success)
            LOG_WARN("Transmission Unsuccessful!");
        else
            LOG_INFO("Transmission Sucessful!");

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // move to next round
        csd_success_signal = false;

        // stop here (only one round for now)
        // stop_signal_called = true;
    }
}

void consumer_thread(CycleStartDetector &csd_obj, ConfigParser &parser, std::atomic<bool> &csd_success_signal)
{
    while (not stop_signal_called)
    {
        csd_obj.consume(csd_success_signal, stop_signal_called);
        if (csd_success_signal)
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
    bool is_central_server;

    if (argc < 2)
        throw std::invalid_argument("ERROR : device address missing! Pass it as first argument to the function call.");

    if (argc < 2)
        is_central_server = false;
    else
        is_central_server = true;

    std::string device_id = argv[1];

    /*----- LOG ------------------------*/
    std::string logFileName = projectDir + "/storage/logs/leaf_" + device_id + "_" + curr_time_str + ".log";
    Logger::getInstance().initialize(logFileName);
    Logger::getInstance().setLogLevel(LOG_LEVEL);

    LOG_INFO_FMT("Starting Calibration routine at %1% ...", is_central_server ? "CENT" : "LEFT");

    /*------ Parse Config -------------*/
    ConfigParser parser(projectDir + "/config/config.conf");
    parser.set_value("device-id", device_id, "str", "USRP device number");
    parser.set_value("storage-folder", projectDir + "/storage", "str", "Location of storage director");

    /*------- USRP setup --------------*/
    USRP_class usrp_obj(parser);

    usrp_obj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_obj.initialize(true);

    parser.set_value("max-rx-packet-size", std::to_string(usrp_obj.max_rx_packet_size), "int", "Max Rx packet size");

    parser.print_values();

    size_t calib_seq_len = usrp_obj.max_rx_packet_size;
    size_t calib_rounds = 10;

    WaveformGenerator wf_gen = WaveformGenerator();
    wf_gen.initialize(wf_gen.UNIT_RAND, calib_seq_len, 1.0, 0, 0, 0, 1.0, 0);
    const auto tx_waveform = wf_gen.generate_waveform();

    /*------ Run CycleStartDetector -------------*/
    double rx_sample_duration_float = 1 / parser.getValue_float("rate");
    uhd::time_spec_t rx_sample_duration = uhd::time_spec_t(rx_sample_duration_float);
    float init_noise_level = usrp_obj.init_background_noise;
    size_t capacity = std::pow(2.0, parser.getValue_int("capacity-pow"));
    PeakDetectionClass peakDet_obj(parser, init_noise_level);
    CycleStartDetector csd_obj(parser, capacity, rx_sample_duration, peakDet_obj);

    /*------ Threads - Consumer / Producer --------*/
    std::atomic<bool> csd_success_signal(false);

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    // setup thread_group
    boost::thread_group thread_group;

    auto my_producer_thread = thread_group.create_thread([=, &usrp_obj, &csd_obj, &peakDet_obj, &parser, &csd_success_signal]()
                                                         { producer_thread(usrp_obj, peakDet_obj, csd_obj, parser, csd_success_signal, homeDirStr, is_central_server, calib_rounds); });

    uhd::set_thread_name(my_producer_thread, "producer_thread");

    // consumer thread
    auto my_consumer_thread = thread_group.create_thread([=, &csd_obj, &parser, &csd_success_signal]()
                                                         { consumer_thread(csd_obj, parser, csd_success_signal); });

    uhd::set_thread_name(my_consumer_thread, "consumer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    return EXIT_SUCCESS;
};