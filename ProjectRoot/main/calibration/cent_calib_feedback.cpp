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

static size_t max_rounds = 10;

void producer_thread(USRP_class &usrp_obj, PeakDetectionClass &peakDet_obj, CycleStartDetector &csd_obj, ConfigParser &parser, std::atomic<bool> &csd_success_signal, std::string homeDirStr)
{
    // subscribe to listen for leaf nodes
    bool response_from_leaf = false;
    std::string leaf_serial = "";
    std::string feedback_topic = "calibration/leaf_feedback";
    std::time_t current_time = std::time(nullptr);

    std::function<void(const std::string &)> leaf_feedback_cb = [&response_from_leaf, &leaf_serial, &current_time](const std::string &payload)
    {
        // Parse the JSON payload
        json json_data = json::parse(payload);
        leaf_serial = json_data["leaf-serial"];
        float leaf_ampl_feedback = json_data["amplitude"].get<float>();
        float leaf_pow_dB_feedback = json_data["pow_dB"].get<float>();
        std::string msg_time = json_data["time"];
        std::time_t parsed_time = convertStrToTime(msg_time);

        if (parsed_time > current_time)
        {
            response_from_leaf = true;
            LOG_DEBUG_FMT("Received response from %1% with amplitude %2%, power %3% dB and time %4%", leaf_serial, leaf_ampl_feedback, leaf_pow_dB_feedback, convertTimeToStr(parsed_time));
        }
    };

    std::string cent_serial = parser.getValue_str("cent-serial");
    std::string client_id = "cent_" + cent_serial;
    MQTTClient &mqttClient = MQTTClient::getInstance(client_id);
    mqttClient.setCallback(feedback_topic, leaf_feedback_cb);

    std::string cent_feedback_topic = "calibration/cent_feedback";

    // create transmit waveform
    WaveformGenerator wf_gen = WaveformGenerator();
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    size_t wf_pad = 0;
    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    auto tx_waveform = wf_gen.generate_waveform();

    size_t round = 1;

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
        while (not response_from_leaf)
        {
            bool transmit_success = usrp_obj.transmission(tx_waveform, uhd::time_spec_t(0.0), stop_signal_called, false);
            if (!transmit_success)
                LOG_WARN("Transmission Unsuccessful!");
            else
                LOG_INFO("Transmission Sucessful!");

            if (stop_signal_called)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        LOG_DEBUG("Received response! Starting to listen...");
        // CycleStartDetector - producer loop
        auto rx_samples = usrp_obj.reception(stop_signal_called, 0, 0.0, uhd::time_spec_t(0.0), false, producer_wrapper);

        if (stop_signal_called)
            break;

        if (csd_success_signal)
        {
            LOG_INFO_FMT("Producer success for round %1%!", round);
            json json_data;
            json_data["leaf-serial"] = leaf_serial;
            json_data["amplitude"] = csd_obj.est_ref_sig_amp;
            json_data["time"] = currentDateTime();

            mqttClient.publish(cent_feedback_topic, json_data.dump(4));
        }
        else
        {
            LOG_WARN("Reception ended without successful detection! Stopping...");
            stop_signal_called = true;
            break;
        }

        if (round >= max_rounds)
            stop_signal_called = true;

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        LOG_INFO_FMT("---------- FINISHED ROUND %1% ----------", round);
        ++round;
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

    /*----- LOG ------------------------*/
    std::string logFileName = projectDir + "/storage/logs/calib_cent_" + curr_time_str + ".log";
    Logger::getInstance().initialize(logFileName);
    Logger::getInstance().setLogLevel(LOG_LEVEL);

    /*------ Parse Config -------------*/
    ConfigParser parser(projectDir + "/config/config.conf");
    parser.set_value("storage-folder", projectDir + "/storage", "str", "Location of storage director");
    std::string cent_serial = parser.getValue_str("cent-serial");

    LOG_INFO_FMT("Starting Calibration routine at Central server : serial %1%", cent_serial);

    /*------- MQTT Client setup -------*/
    std::string client_id = "cent_" + cent_serial;
    MQTTClient &mqttClient = MQTTClient::getInstance(client_id);
    LOG_INFO(parser.print_json());
    mqttClient.publish("config/run_config_info", parser.print_json());

    /*------- USRP setup --------------*/
    USRP_class usrp_obj(parser);

    usrp_obj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_obj.initialize(true);

    parser.set_value("max-rx-packet-size", std::to_string(usrp_obj.max_rx_packet_size), "int", "Max Rx packet size");

    parser.print_values();

    /*------ Run CycleStartDetector -------------*/
    double rx_sample_duration_float = 1 / parser.getValue_float("rate");
    uhd::time_spec_t rx_sample_duration = uhd::time_spec_t(rx_sample_duration_float);
    float init_noise_ampl = usrp_obj.init_noise_ampl;
    size_t capacity = std::pow(2.0, parser.getValue_int("capacity-pow"));
    PeakDetectionClass peakDet_obj(parser, init_noise_ampl);
    CycleStartDetector csd_obj(parser, capacity, rx_sample_duration, peakDet_obj);
    csd_obj.is_correct_cfo = false;

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