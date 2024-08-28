#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"
#include "usrp_class.hpp"
#include "waveforms.hpp"
#include "cyclestartdetector.hpp"
#include "MQTTClient.hpp"

// #include <cstdlib>  // For system(), getenv()
// #include <unistd.h> // For getpid(), getppid()

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
    size_t wf_gap = 0; // size_t(parser.getValue_float("tx-gap-microsec") / 1e6 * usrp_obj.tx_rate);
    size_t wf_pad = 0; // size_t(parser.getValue_int("Ref-padding-mul") * wf_len);
    size_t zfc_q = parser.getValue_int("test-zfc-m");
    size_t rand_seed = parser.getValue_int("test-zfc-m");
    float min_ch_scale = parser.getValue_float("min-e2e-amp");
    wf_gen.initialize(wf_gen.UNIT_RAND, wf_len, wf_reps, wf_gap, wf_pad, zfc_q, 1.0, rand_seed);
    auto unit_rand_samples = wf_gen.generate_waveform();
    std::string storage_dir = parser.getValue_str("storage-folder");
    std::string device_id = parser.getValue_str("device-id");
    std::string curr_time_str = currentDateTimeFilename();
    // std::ofstream outfile;
    // save_stream_to_file(storage_dir + "/logs/transmit_unit_rand_" + device_id + "_" + curr_time_str + ".dat", outfile, unit_rand_samples);
    // if (outfile.is_open())
    //     outfile.close();

    std::string client_id = "leaf_" + device_id;
    MQTTClient &mqttClient = MQTTClient::getInstance(client_id);
    std::string CFO_topic = "calibration/CFO/" + device_id;
    std::string scale_topic = "otac/simdata/scale/" + device_id;
    auto format_scale_data = [](float scale) -> std::string
    {
        std::string text = "{'scale':" + floatToStringWithPrecision(scale, 8) + ", 'time': " + currentDateTime() + "}";
        return text;
    };

    // This function is called by the receiver as a callback everytime a frame is received
    auto producer_wrapper = [&csd_obj, &csd_success_signal](const std::vector<std::complex<float>> &samples, const size_t &sample_size, const uhd::time_spec_t &sample_time)
    {
        csd_obj.produce(samples, sample_size, sample_time, stop_signal_called);

        if (csd_success_signal)
            return true;
        else
            return false;
    };

    size_t alt_wf_gap = 10000, inner_wf_gap = 1000;
    size_t tx_waveform_gap = int(usrp_obj.tx_rate * inner_wf_gap / 1e6); // 10 millisec gap

    while (not stop_signal_called)
    {
        LOG_INFO_FMT("-------------- Round %1% ------------", round);

        // CycleStartDetector - producer loop
        // debug
        std::string curr_time_str = currentDateTimeFilename();

        std::string ref_datfile = storage_dir + "/logs/saved_ref_leaf_" + device_id + "_" + curr_time_str + ".dat";
        csd_obj.saved_ref_filename = ref_datfile;

        auto rx_samples = usrp_obj.reception(stop_signal_called, 0, 0, uhd::time_spec_t(0.0), false, producer_wrapper);

        if (stop_signal_called)
            break;

        // publish CFO value
        mqttClient.publish(CFO_topic, floatToStringWithPrecision(csd_obj.cfo, 8), true);
        // publish last scale factor used
        float curr_scaling = min_ch_scale / csd_obj.calibration_ratio / csd_obj.est_ref_sig_amp;
        mqttClient.publish(scale_topic, format_scale_data(curr_scaling), true);

        LOG_INFO_FMT("------------------ Producer finished for round %1%! --------------", round);
        ++round;

        // Transmission after cyclestartdetector
        uhd::time_spec_t tx_start_timer = csd_obj.csd_tx_start_timer;
        LOG_INFO_FMT("Current timer %1% and Tx start timer %2%.", usrp_obj.usrp->get_time_now().get_real_secs(), tx_start_timer.get_real_secs());

        // adjust for CFO
        int counter = 0;
        std::vector<std::complex<float>> single_waveform(unit_rand_samples.begin(), unit_rand_samples.end());
        for (auto &samp : single_waveform)
        {
            if (csd_obj.cfo != 0.0)
                samp *= curr_scaling * std::complex<float>(std::cos(csd_obj.cfo * counter), std::sin(csd_obj.cfo * counter));
            else
                samp *= curr_scaling;
            counter++;
        }

        LOG_DEBUG_FMT("Transmitting waveform UNIT_RAND (len=%6%, L=%1%, rand_seed=%2%, R=%3%, gap=%4%, scale=%5%)", wf_len, zfc_q, wf_reps, wf_gen.wf_gap, curr_scaling, single_waveform.size());

        // pad front and end --  check if this improves
        // single_waveform.insert(single_waveform.begin(), 1000, std::complex<float>(0.0, 0.0));
        // single_waveform.insert(single_waveform.end(), 1000, std::complex<float>(0.0, 0.0));

        for (int j = 0; j < 50; ++j)
        {
            std::vector<std::complex<float>> tx_waveform;
            for (int i = 0; i < 10; ++i)
            {
                tx_waveform.insert(tx_waveform.end(), single_waveform.begin(), single_waveform.end());
                tx_waveform.insert(tx_waveform.end(), tx_waveform_gap, std::complex<float>(0.0, 0.0));
            }

            bool transmit_success = usrp_obj.transmission(tx_waveform, tx_start_timer, stop_signal_called, true);
            if (!transmit_success)
                LOG_WARN("Transmission Unsuccessful!");
            else
                LOG_INFO("Transmission Sucessful!");

            std::this_thread::sleep_for(std::chrono::microseconds(int((tx_start_timer - usrp_obj.usrp->get_time_now()).get_real_secs() * 1e6) + inner_wf_gap + alt_wf_gap - 1000));
            tx_start_timer = usrp_obj.usrp->get_time_now() + uhd::time_spec_t(alt_wf_gap / 1e6);
        }

        std::this_thread::sleep_for(std::chrono::microseconds(int((tx_start_timer - usrp_obj.usrp->get_time_now()).get_real_secs() * 1e6) + 100000));

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
    if (argc > 2)
    {
        size_t test_zfc_q = std::stoi(argv[2]);
        parser.set_value("test-zfc-m", std::to_string(test_zfc_q), "int", "ZFC param `m` for test signal.");
    }
    else
        parser.set_value("test-zfc-m", std::to_string(41), "int", "ZFC param `m` for test signal.");
    parser.set_value("storage-folder", projectDir + "/storage", "str", "Location of storage director");

    /*------- MQTT Client setup -------*/
    std::string client_id = "leaf_" + device_id;
    MQTTClient &mqttClient = MQTTClient::getInstance(client_id);
    // subscribe to CFO topic
    // -> register message callback
    float last_cfo = 0.0, calibration_ratio = 1.0;
    bool got_calib_ratio = false, got_cfo = false;
    std::string cent_serial = parser.getValue_str("cent-serial");
    std::string CFO_topic = "calibration/CFO/" + device_id, calib_topic = "calibration/ratio/" + cent_serial + "/" + device_id;
    std::function<void(const std::string &)> CFO_callback = [&last_cfo, &got_cfo](const std::string &payload)
    {
        // Parse the JSON payload
        last_cfo = std::stof(payload);
        LOG_DEBUG_FMT("MQTT >> CFO : %1%", last_cfo);
        got_cfo = true;
    };

    std::function<void(const std::string &)> calib_ratio_callback = [&calibration_ratio, &got_calib_ratio](const std::string &payload)
    {
        try
        {
            // Parse the JSON payload
            json jsonData = json::parse(payload);

            // Update the temperature variable if the key exists
            if (jsonData.contains("amp_ratio_mean"))
            {
                calibration_ratio = jsonData["amp_ratio_mean"].get<float>();
                LOG_DEBUG_FMT("MQTT >> Calib ratio : %1%", calibration_ratio);
                got_calib_ratio = true;
                // update_device_config_cfo(device_id, jsonData["cfo"].get<float>());
            }
        }
        catch (const json::parse_error &e)
        {
            LOG_WARN_FMT("MQTT >> JSON parsing error: ", e.what());
        }
    };

    size_t max_timer = 30, timer = 0;
    mqttClient.setCallback(CFO_topic, CFO_callback);
    while (got_cfo == false && timer < max_timer)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        timer++;
    }
    mqttClient.unsubscribe(CFO_topic);

    timer = 0;
    mqttClient.setCallback(calib_topic, calib_ratio_callback);
    while (got_calib_ratio == false && timer < max_timer)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        timer++;
    }
    mqttClient.unsubscribe(calib_topic);

    /*------- USRP setup --------------*/
    USRP_class usrp_obj(parser);

    // external reference
    usrp_obj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_obj.initialize();

    parser.set_value("max-rx-packet-size", std::to_string(usrp_obj.max_rx_packet_size), "int", "Max Rx packet size");

    parser.print_values();

    /*------ Run CycleStartDetector -------------*/
    double rx_sample_duration_float = 1 / parser.getValue_float("rate");
    uhd::time_spec_t rx_sample_duration = uhd::time_spec_t(rx_sample_duration_float);
    float init_noise_ampl = usrp_obj.init_noise_ampl;
    size_t capacity = std::pow(2.0, parser.getValue_int("capacity-pow"));
    PeakDetectionClass peakDet_obj(parser, init_noise_ampl);
    CycleStartDetector csd_obj(parser, capacity, rx_sample_duration, peakDet_obj);
    // float last_cfo = obtain_last_cfo(device_id);
    csd_obj.cfo = last_cfo;
    csd_obj.calibration_ratio = calibration_ratio;

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