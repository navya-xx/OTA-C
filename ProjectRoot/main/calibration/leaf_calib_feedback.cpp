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
    // subscribe to listen for cent nodes
    bool response_from_cent = false;
    std::string leaf_serial = parser.getValue_str("device-id");
    std::string cent_serial = parser.getValue_str("cent-serial");
    float cent_ampl_feedback = 0.0;
    std::string feedback_topic = "calibration/cent_feedback";
    std::time_t current_time = std::time(nullptr);

    std::function<void(const std::string &)> cent_feedback_cb = [&response_from_cent, &leaf_serial, &cent_serial, &current_time](const std::string &payload)
    {
        // Parse the JSON payload
        json json_data = json::parse(payload);
        std::string response_leaf_serial = json_data["leaf-serial"];
        cent_ampl_feedback = json_data["amplitude"].get<float>();
        std::string msg_time = json_data["time"];
        std::time_t parsed_time = convertStrToTime(msg_time);

        if (parsed_time > current_time && response_leaf_serial == leaf_serial)
        {
            response_from_cent = true;
            LOG_DEBUG_FMT("Received response from %1% with amplitude %2% and time %3%", cent_serial, cent_ampl_feedback, convertTimeToStr(parsed_time));
        }
    };

    std::string client_id = "leaf_" + cent_serial;
    MQTTClient &mqttClient = MQTTClient::getInstance(client_id);
    mqttClient.setCallback(feedback_topic, cent_feedback_cb);

    std::string leaf_feedback_topic = "calibration/leaf_feedback";

    WaveformGenerator wf_gen = WaveformGenerator();
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    size_t wf_pad = 0;
    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    auto tx_waveform = wf_gen.generate_waveform();

    // This function is called by the receiver as a callback everytime a frame is received
    auto producer_wrapper = [&csd_obj, &csd_success_signal](const std::vector<std::complex<float>> &samples, const size_t &sample_size, const uhd::time_spec_t &sample_time)
    {
        csd_obj.produce(samples, sample_size, sample_time, stop_signal_called);

        if (csd_success_signal)
            return true;
        else
            return false;
    };

    size_t round = 1;

    while (not stop_signal_called)
    {
        LOG_INFO_FMT("-------------- Round %1% ------------", round);

        // CycleStartDetector - producer loop

        auto rx_samples = usrp_obj.reception(stop_signal_called, 0, 0.0, uhd::time_spec_t(0.0), false, producer_wrapper);

        if (stop_signal_called)
            break;

        if (csd_success_signal)
        {
            LOG_INFO_FMT("Producer success for round %1%!", round);
            json json_data;
            json_data["leaf-serial"] = device_id;
            json_data["amplitude"] = csd_obj.est_ref_sig_pow;

            mqttClient.publish(leaf_feedback_topic, json_data);
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
    if (argc < 1)
        throw std::invalid_argument("ERROR : Requires serial of the leaf USRP.");

    std::string device_id = argv[1];

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
    parser.set_value("device-id", device_id, "str", "USRP device number");
    parser.set_value("storage-folder", projectDir + "/storage", "str", "Location of storage director");
    std::string cent_serial = parser.getValue_str("cent-serial");

    LOG_INFO_FMT("Starting Calibration routine at Central server : serial %1%", cent_serial);

    /*------- MQTT Client setup -------*/
    float last_cfo = 0.0;
    std::string client_id = "leaf_" + device_id;
    MQTTClient &mqttClient = MQTTClient::getInstance(client_id);
    bool got_cfo = false;

    std::string CFO_topic = "calibration/CFO/" + device_id;

    std::function<void(const std::string &)> CFO_callback = [&last_cfo, &got_cfo](const std::string &payload)
    {
        // Parse the JSON payload
        last_cfo = std::stof(payload);
        LOG_DEBUG_FMT("MQTT >> CFO : %1%", last_cfo);
        got_cfo = true;
    };

    size_t max_timer = 30, timer = 0;
    mqttClient.setCallback(CFO_topic, CFO_callback);
    while (got_cfo == false && timer < max_timer)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        timer++;
    }
    mqttClient.unsubscribe(CFO_topic);

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
    csd_obj.cfo = last_cfo;

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