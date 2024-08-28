#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"
#include "usrp_class.hpp"
#include "waveforms.hpp"
#include "cyclestartdetector.hpp"
#include "MQTTClient.hpp"

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
    size_t round = 1, calib_retry = 0, max_calib_retry = 10;
    std::vector<std::complex<float>> buff(max_rx_packet_size);

    WaveformGenerator wf_gen = WaveformGenerator();
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    size_t wf_pad = size_t(parser.getValue_int("Ref-padding-mul") * N_zfc);
    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    auto tx_waveform = wf_gen.generate_waveform();

    std::string device_id = parser.getValue_str("device-id");
    size_t max_num_rounds = parser.getValue_int("max-calib-rounds");

    MQTTClient &mqttClient = MQTTClient::getInstance();

    // This function is called by the receiver as a callback everytime a frame is received
    auto producer_wrapper = [&csd_obj, &csd_success_signal](const std::vector<std::complex<float>> &samples, const size_t &sample_size, const uhd::time_spec_t &sample_time)
    {
        csd_obj.produce(samples, sample_size, sample_time, stop_signal_called);

        if (csd_success_signal)
            return true;
        else
            return false;
    };

    while (not stop_signal_called)
    {
        LOG_INFO_FMT("-------------- Round %1% ------------", round);

        // CycleStartDetector - producer loop

        auto rx_samples = usrp_obj.reception(stop_signal_called, 0, 0.0, uhd::time_spec_t(0.0), false, producer_wrapper);

        if (stop_signal_called)
            break;

        if (csd_success_signal)
        {
            LOG_INFO_FMT("------------------ Producer finished for round %1%! --------------", round);
            // append_value_with_timestamp(ref_calib_file, calib_file, floatToStringWithPrecision(csd_obj.est_ref_sig_amp, 8));
            std::string json_data;
            if (is_cent)
                json_data = create_calib_data_str(parser.getValue_str("leaf-id"), device_id, usrp_obj.tx_gain, usrp_obj.rx_gain, csd_obj.est_ref_sig_amp);
            else
                json_data = create_calib_data_str(parser.getValue_str("cent-id"), device_id, usrp_obj.tx_gain, usrp_obj.rx_gain, csd_obj.est_ref_sig_amp);

            mqttClient.publish(calib_topic, json_data);
            ++round;
            calib_retry = 0;
        }
        else if (is_cent)
        {
            LOG_INFO_FMT("No calibration signal received in Round %1%. Re-transmitting...", round);
            if (++calib_retry > max_calib_retry)
            {
                round = max_num_rounds + 1; // end it here
                LOG_INFO_FMT("Ending calibration! No calibration signal received from leaf-node for %1% rounds.", calib_retry);
            }
        }
        else
        {
            LOG_WARN("Reception ended without successful detection! Stopping...");
            stop_signal_called = true;
            break;
        }

        csd_success_signal = false;

        if (round > max_num_rounds)
        {
            stop_signal_called = true;
            break;
        }
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
    int num_runs = int(parser.getValue_int("num-test-runs"));
    if (argc > 2)
    {
        num_runs = std::stoi(argv[2]);
        parser.set_value("num-test-runs", std::to_string(num_runs), "int");
    }

    parser.set_value("storage-folder", projectDir + "/storage", "str", "Location of storage director");

    /*------- USRP setup --------------*/
    USRP_class usrp_obj(parser);

    usrp_obj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_obj.initialize();

    parser.set_value("max-rx-packet-size", std::to_string(usrp_obj.max_rx_packet_size), "int", "Max Rx packet size");

    parser.print_values();

    /*------- Receive calibration signal ------------*/
    /*------ Run CycleStartDetector -------------*/
    double rx_sample_duration_float = 1 / parser.getValue_float("rate");
    uhd::time_spec_t rx_sample_duration = uhd::time_spec_t(rx_sample_duration_float);
    float init_noise_ampl = usrp_obj.init_noise_ampl;
    size_t capacity = std::pow(2.0, parser.getValue_int("capacity-pow"));
    PeakDetectionClass peakDet_obj(parser, init_noise_ampl);
    CycleStartDetector csd_obj(parser, capacity, rx_sample_duration, peakDet_obj);

    csd_obj.tx_wait_microsec = 0.3 * 1e6;

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

    /*------- Transmit CSD ref and receive ----------*/

    float tx_wait_microsec = parser.getValue_float("start-tx-wait-microsec");

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

        // Receive samples for a fixed duration and return
        double rx_duration_secs = 1.0;
        std::this_thread::sleep_for(std::chrono::microseconds(int(tx_wait_microsec)));
        auto received_samples = usrp_obj.reception(stop_signal_called, 0, rx_duration_secs, uhd::time_spec_t(0.0), true);

        // LOG_INFO_FMT("--------------- FINISHED : %1% ----------------", i);
    }

    return EXIT_SUCCESS;
};