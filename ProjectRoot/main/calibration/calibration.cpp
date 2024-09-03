#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"
#include "usrp_class.hpp"
#include "waveforms.hpp"
#include "cyclestartdetector.hpp"
#include "MQTTClient.hpp"
#include <filesystem>

#define LOG_LEVEL LogLevel::DEBUG
static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

static std::string calib_topic = "calibration/results";

std::string create_calib_data_str(const std::string tx_dev, const std::string rx_dev, const float tx_gain, const float rx_gain, const float &amplitude)
{
    json jdata;
    jdata["tx_dev"] = tx_dev;
    jdata["rx_dev"] = rx_dev;
    jdata["tx_gain"] = tx_gain;
    jdata["rx_gain"] = rx_gain;
    jdata["amplitude"] = amplitude;
    jdata["time"] = currentDateTime();
    return jdata.dump(4);
}

void producer_thread(USRP_class &usrp_obj, PeakDetectionClass &peakDet_obj, CycleStartDetector &csd_obj, ConfigParser &parser, std::atomic<bool> &csd_success_signal, std::string homeDirStr, const bool &is_cent, const size_t &max_num_rounds)
{
    // reception/producer params
    size_t max_rx_packet_size = usrp_obj.max_rx_packet_size;
    size_t round = 1, calib_retry = 0, max_calib_retry = 10;
    std::vector<std::complex<float>> buff(max_rx_packet_size);

    WaveformGenerator wf_gen = WaveformGenerator();
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    size_t wf_pad = 10 * size_t(reps_zfc * N_zfc);
    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    wf_gen.pad_scale = 0.05;
    auto tx_waveform = wf_gen.generate_waveform();

    std::string device_id = parser.getValue_str("device-id");
    if (is_cent)
        std::string leaf_id = parser.getValue_str("leaf-id");
    else
        std::string cent_id = parser.getValue_str("cent-id");

    MQTTClient &mqttClient = MQTTClient::getInstance();

    float rx_duration = is_cent ? 1.0 : 0.0; // fix reception duration for cent node
    double sleep_sec = 0.2;

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

        // std::this_thread::sleep_for(std::chrono::milliseconds(sleep_millisec));
    }

    while (not stop_signal_called)
    {
        LOG_INFO_FMT("-------------- Round %1% ------------", round);

        std::this_thread::sleep_for(std::chrono::milliseconds(int(sleep_sec * 1e3)));

        // CycleStartDetector - producer loop

        auto rx_samples = usrp_obj.reception(stop_signal_called, 0, rx_duration, uhd::time_spec_t(0.0), false, producer_wrapper);

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

        // Transmission after cyclestartdetector
        uhd::time_spec_t tx_start_timer = usrp_obj.usrp->get_time_now() + uhd::time_spec_t(sleep_sec);
        LOG_INFO_FMT("Current timer %1% and Tx start timer %2%.", usrp_obj.usrp->get_time_now().get_real_secs(), tx_start_timer.get_real_secs());

        if (!is_cent)
        {
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
        }

        // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        bool transmit_success = usrp_obj.transmission(tx_waveform, tx_start_timer, stop_signal_called, true);
        if (!transmit_success)
            LOG_WARN("Transmission Unsuccessful!");
        else
            LOG_INFO("Transmission Sucessful!");

        // std::this_thread::sleep_for(std::chrono::microseconds(int((fix_wait_time - 1.0 + 0.1) * 1e6)));

        // move to next round
        csd_success_signal = false;

        // stop here (only one round for now)
        // stop_signal_called = true;

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
    std::string leaf_id = ";";
    bool is_central_server = false;

    if (argc < 4)
        throw std::invalid_argument("ERROR : Calibration requires 3 mandatory arguments -> (device_type <cent, leaf> | this_device_serial | counterpart_device_serial)");

    std::string device_type = argv[1];
    std::string device_id = argv[2];
    std::string counterpart_id = argv[3];

    /*----- LOG ------------------------*/
    std::string logFileName = projectDir + "/storage/logs/leaf_" + device_id + "_" + curr_time_str + ".log";
    Logger::getInstance().initialize(logFileName);
    Logger::getInstance().setLogLevel(LOG_LEVEL);

    /*------ Parse Config -------------*/
    ConfigParser parser(projectDir + "/config/config.conf");
    parser.set_value("device-id", device_id, "str", "USRP device number");
    parser.set_value("storage-folder", projectDir + "/storage", "str", "Location of storage director");
    if (device_type == "cent")
    {
        parser.set_value("leaf-id", counterpart_id, "str", "leaf node serial number as identifier");
        is_central_server = true;
    }
    else if (device_type == "leaf")
        parser.set_value("cent-id", counterpart_id, "str", "cent node serial number as identifier");
    else
        throw std::invalid_argument("Incorrect device type! Valid options are (cent or leaf).");

    LOG_INFO_FMT("Starting Calibration routine at %1% ...", is_central_server ? "CENT" : "LEAF");

    /*------- MQTT Client setup -------*/
    std::string client_id = device_type + "_" + device_id;
    MQTTClient &mqttClient = MQTTClient::getInstance(client_id);
    if (device_type == "cent")
    {
        LOG_INFO(parser.print_json());
        mqttClient.publish("config/run_config_info", parser.print_json());
    }

    /*------- USRP setup --------------*/
    USRP_class usrp_obj(parser);

    usrp_obj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_obj.initialize(true);

    parser.set_value("max-rx-packet-size", std::to_string(usrp_obj.max_rx_packet_size), "int", "Max Rx packet size");

    parser.print_values();

    size_t max_calib_rounds = parser.getValue_int("max-calib-rounds");

    /*------ Run CycleStartDetector -------------*/
    double rx_sample_duration_float = 1 / parser.getValue_float("rate");
    uhd::time_spec_t rx_sample_duration = uhd::time_spec_t(rx_sample_duration_float);
    float init_noise_ampl = usrp_obj.init_noise_ampl;
    size_t capacity = std::pow(2.0, parser.getValue_int("capacity-pow"));
    PeakDetectionClass peakDet_obj(parser, init_noise_ampl);
    CycleStartDetector csd_obj(parser, capacity, rx_sample_duration, peakDet_obj);

    csd_obj.tx_wait_microsec = 0.3 * 1e6;

    if (is_central_server)
        csd_obj.is_correct_cfo = false;

    /*------ Threads - Consumer / Producer --------*/
    std::atomic<bool> csd_success_signal(false);

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    // setup thread_group
    boost::thread_group thread_group;

    auto my_producer_thread = thread_group.create_thread([=, &usrp_obj, &csd_obj, &peakDet_obj, &parser, &csd_success_signal]()
                                                         { producer_thread(usrp_obj, peakDet_obj, csd_obj, parser, csd_success_signal, homeDirStr, is_central_server, max_calib_rounds); });

    uhd::set_thread_name(my_producer_thread, "producer_thread");

    // consumer thread
    auto my_consumer_thread = thread_group.create_thread([=, &csd_obj, &parser, &csd_success_signal]()
                                                         { consumer_thread(csd_obj, parser, csd_success_signal); });

    uhd::set_thread_name(my_consumer_thread, "consumer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    return EXIT_SUCCESS;
};