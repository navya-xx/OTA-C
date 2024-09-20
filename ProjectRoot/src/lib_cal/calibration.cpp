#include "calibration.hpp"

Calibration::Calibration(
    USRP_class &usrp_obj_,
    ConfigParser &parser_,
    const std::string &device_id_,
    const std::string &counterpart_id_,
    const std::string &device_type_,
    bool &signal_stop_called_) : usrp_obj(&usrp_obj_),
                                 parser(parser_),
                                 device_id(device_id_),
                                 counterpart_id(counterpart_id_),
                                 device_type(device_type_),
                                 signal_stop_called(signal_stop_called_),
                                 csd_obj(nullptr),
                                 peak_det_obj(nullptr),
                                 ref_waveform()
{
    if (device_type == "cent")
    {
        cent_id = device_id;
        leaf_id = counterpart_id;
    }
    else if (device_type == "leaf")
    {
        cent_id = counterpart_id;
        leaf_id = device_id;
    }
};

Calibration::~Calibration()
{

    if (producer_thread.joinable())
        producer_thread.join();
    if (consumer_thread.joinable())
        consumer_thread.join();
}

void Calibration::initialize_peak_det_obj()
{
    peak_det_obj = std::make_unique<PeakDetectionClass>(parser, usrp_obj->init_noise_ampl);
}

void Calibration::initialize_csd_obj()
{
    size_t capacity = std::pow(2.0, parser.getValue_int("capacity-pow"));
    double rx_sample_duration_float = 1 / parser.getValue_float("rate");
    uhd::time_spec_t rx_sample_duration = uhd::time_spec_t(rx_sample_duration_float);
    csd_obj = std::make_unique<CycleStartDetector>(parser, capacity, rx_sample_duration, *peak_det_obj);
}

void Calibration::get_mqtt_topics()
{
    if (device_type == "leaf")
        client_id = leaf_id;
    else if (device_type == "cent")
        client_id = cent_id;

    MQTTClient &mqttClient = MQTTClient::getInstance(client_id); // for publishing data

    // populate topics
    CFO_topic = mqttClient.topics->getValue_str("CFO") + client_id;
    flag_topic_cent = mqttClient.topics->getValue_str("calib-flags") + cent_id; // flags are set by the leaf
    flag_topic_leaf = mqttClient.topics->getValue_str("calib-flags") + leaf_id; // flags are set by the leaf
    mctest_topic = mqttClient.topics->getValue_str("calib-mctest") + cent_id;

    ltoc_topic = mqttClient.topics->getValue_str("calib-ltoc") + cent_id;       // ltoc sigpow is sent by cent
    ctol_topic = mqttClient.topics->getValue_str("calib-ctol") + leaf_id;       // ctol sigpow is sent by cent
    cal_scale_topic = mqttClient.topics->getValue_str("calib-scale") + leaf_id; // flags are set by the leaf

    tx_gain_topic = mqttClient.topics->getValue_str("tx-gain") + device_id;
    rx_gain_topic = mqttClient.topics->getValue_str("rx-gain") + device_id;
    full_scale_topic = mqttClient.topics->getValue_str("full-scale") + leaf_id; // flags are set by the leaf
}

void Calibration::generate_waveform()
{
    WaveformGenerator wf_gen;
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc"); // extra long ref signal for stability
    size_t wf_pad = size_t(parser.getValue_int("Ref-padding-mul") * N_zfc);

    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, wf_pad, q_zfc, calib_sig_scale, 0);
    ref_waveform = wf_gen.generate_waveform();
}

bool Calibration::initialize()
{
    csd_success_flag = false;
    calibration_successful = false;
    calibration_ends = false;
    ltoc = -1.0, ctol = -1.0;
    try
    {
        initialize_peak_det_obj();
        initialize_csd_obj();
        generate_waveform();
        get_mqtt_topics();

        MQTTClient &mqttClient = MQTTClient::getInstance(device_id);
        if (device_type == "cent")
        {
            mqttClient.setCallback(flag_topic_leaf, [this](const std::string &payload)
                                   { callback_detect_flags(payload); });
        }
        else if (device_type == "leaf")
        {
            mqttClient.setCallback(ltoc_topic, [this](const std::string &payload)
                                   { callback_update_ltoc(payload); });
            mqttClient.setCallback(flag_topic_cent, [this](const std::string &payload)
                                   { callback_detect_flags(payload); });
            // std::string temp;
            // if (mqttClient.temporary_listen_for_last_value(temp, full_scale_topic, 10, 30))
            //     full_scale = std::stof(temp);
        }
        return true;
    }
    catch (std::exception &e)
    {
        LOG_WARN_FMT("Calibration Initialization failed with ERROR: %1%", e.what());
        return false;
    }
};

void Calibration::run()
{
    // warm up the device
    for (int i = 0; i < 1; ++i)
    {
        usrp_obj->perform_tx_test();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (device_type == "leaf")
        producer_thread = boost::thread(&Calibration::producer_leaf, this);
    else if (device_type == "cent")
        producer_thread = boost::thread(&Calibration::producer_cent, this);

    consumer_thread = boost::thread(&Calibration::consumer, this);
};

void Calibration::run_scaling_tests()
{
    scaling_test_ends = false;

    // warm up the device
    for (int i = 0; i < 5; ++i)
    {
        usrp_obj->perform_tx_test();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        usrp_obj->perform_rx_test();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    end_flag = false;

    if (device_type == "leaf")
        producer_thread = boost::thread(&Calibration::run_scaling_tests_leaf, this);
    else if (device_type == "cent")
        producer_thread = boost::thread(&Calibration::run_scaling_tests_cent, this);

    consumer_thread = boost::thread(&Calibration::consumer, this);
}

void Calibration::stop()
{
    csd_success_flag.store(true);
    calibration_ends = true;
    scaling_test_ends = true;

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    LOG_INFO("Deleting Calibration Class object!");
    delete this;
}

// checks whether two values are close to each other, based on tolerance value set inside the function
bool Calibration::proximity_check(const float &val1, const float &val2)
{
    float dist_norm = sqrt((val1 - val2) * (val1 - val2) / (val1 * val1));
    LOG_DEBUG_FMT("Error - tol : %1% - %2% = %3%", dist_norm, proximity_tol, dist_norm - proximity_tol);
    if (dist_norm < proximity_tol)
    {
        if (current_reps_cal < total_reps_cal)
        {
            current_reps_cal++;
            return false;
        }
        else
            return true;
    }
    else
    {
        current_reps_cal = 0;
        return false;
    }
}

// callback to update ltoc value
void Calibration::callback_update_ltoc(const std::string &payload)
{
    try
    {
        json jsonData = json::parse(payload);
        if (jsonData.contains("value"))
        {
            std::string temp_ltoc = jsonData["value"];
            ltoc = std::stof(temp_ltoc);
            LOG_DEBUG_FMT("MQTT >> LTOC received = %1%", ltoc);
            recv_success = true;
            if (ctol < 0)
                LOG_WARN("CTOL is not updated yet!");
            else if (proximity_check(ctol, ltoc))
                calibration_successful = true;
        }
    }
    catch (const json::parse_error &e)
    {
        LOG_WARN_FMT("MQTT >> JSON parsing error : %1%", e.what());
        LOG_WARN_FMT("Incorrect JSON string = %1%", payload);
    }
}

void Calibration::callback_detect_flags(const std::string &payload)
{
    try
    {
        json jsonData = json::parse(payload);
        if (jsonData.contains("value"))
        {
            std::string flag_value = jsonData["value"];
            if (flag_value == "recv")
                recv_flag = true;
            else if (flag_value == "retx")
                retx_flag = true;
            else if (flag_value == "end")
                end_flag = true;
            else
                LOG_WARN_FMT("MQTT >> Flag %1% does not match any.", jsonData["value"]);
        }
    }
    catch (const json::parse_error &e)
    {
        LOG_WARN_FMT("MQTT >> JSON parsing error : %1%", e.what());
        LOG_WARN_FMT("Incorrect JSON string = %1%", payload);
    }
}

void Calibration::producer_leaf()
{
    MQTTClient &mqttClient = MQTTClient::getInstance(leaf_id);
    float usrp_noise_power = usrp_obj->init_noise_ampl * usrp_obj->init_noise_ampl;

    size_t round = 1;
    bool save_ref_file = false;

    while (not signal_stop_called && round++ < max_total_round)
    {
        LOG_INFO_FMT("-------------- Receiving Round %1% ------------", round);

        if (save_ref_file)
        {
            std::string homeDirStr = get_home_dir();
            std::string curr_datetime = currentDateTimeFilename();
            csd_obj->saved_ref_filename = homeDirStr + "/OTA-C/ProjectRoot/storage/saved_ref_file_" + device_id + "_" + curr_datetime + ".dat";
        }

        // receive multiple signals to have a stable estimate
        size_t num_est = 5, est_count = 0;
        std::vector<float> ctol_vec;
        bool reception_failed = false;
        while (est_count < num_est and not signal_stop_called)
        {
            float ctol_temp;
            size_t recv_counter = 0;
            while (not reception(ctol_temp) and recv_counter++ < 10)
                LOG_WARN_FMT("Attempt %1% : Reception of REF signal failed! Keep receiving...", recv_counter);

            if (ctol_temp > min_sigpow_mul * usrp_noise_power)
            {
                ctol_vec.emplace_back(ctol_temp);
                LOG_INFO_FMT("Rx power of signal from cent = %1%", ctol_temp);
                // publish
                mqttClient.publish(ctol_topic, mqttClient.timestamp_float_data(ctol_temp), false);
            }
            else
            {
                LOG_WARN("Received Rx power of the signal is too low");
                reception_failed = true;
            }

            est_count++;
            csd_success_flag = false;
        }

        if (reception_failed)
            continue;

        float ctol_mean = 0.0;
        for (auto val : ctol_vec)
            ctol_mean += val;
        ctol = ctol_mean / ctol_vec.size();
        LOG_INFO_FMT("Average Rx power of signal from cent = %1%", ctol);
        mqttClient.publish(flag_topic_leaf, mqttClient.timestamp_str_data("recv"), false);

        if (signal_stop_called)
            break;

        // Update Tx/Rx gains based on ltoc values obtained
        size_t leaf_tx_round = 1;
        while (not signal_stop_called && not calibration_successful && leaf_tx_round++ < max_num_tx_rounds)
        {
            // based on ratio between ctol and ltoc rssi values, update TX gain of leaf node
            if (ltoc > 0 && recv_success)
            {
                if (not calibrate_gains(mqttClient))
                    break; // tx_gain adjustment alone is not sufficient -- break and restart with reception again
            }

            // Transmit scaled REF
            LOG_INFO_FMT("-------------- Transmit Round %1% ------------", leaf_tx_round);
            while (not signal_stop_called and not recv_flag)
            {
                transmission(full_scale);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        if (calibration_successful)
        {
            on_calib_success(mqttClient);
            break;
        }

        // increase proximity_tol to counter randomness
        proximity_tol = proximity_tol * std::max(1.0, std::ceil(double(round / 10)));
    }

    calibration_ends = true;
};

void Calibration::producer_cent()
{
    MQTTClient &mqttClient = MQTTClient::getInstance(device_id);
    float usrp_noise_power = usrp_obj->init_noise_ampl * usrp_obj->init_noise_ampl;

    // reception/producer params
    size_t round = 1;

    while (not signal_stop_called && not end_flag && round++ < max_total_round)
    {
        LOG_INFO_FMT("-------------- Transmit Round %1% ------------", round);

        // Transmit REF
        while (not recv_flag && not end_flag && not signal_stop_called)
        {
            transmission();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (signal_stop_called || end_flag)
            break;

        // MQTT message received -- start reception
        if (recv_flag)
            recv_flag = false;
        else
            LOG_WARN("Receive flag is not set! Should not reach here!!!");

        // start receiving REF signal
        float ltoc_temp = 0.0;
        size_t recv_counter = 0;
        csd_success_flag = false;
        bool receive_flag = false;
        // receive multiple signals to have a stable estimate
        size_t num_est = 5, est_count = 0;
        std::vector<float> ltoc_vec;
        bool reception_failed = false;
        while (est_count < num_est and not signal_stop_called)
        {
            float ltoc_temp;
            size_t recv_counter = 0;
            while (not reception(ltoc_temp) and recv_counter++ < 10)
                LOG_WARN_FMT("Attempt %1% : Reception of REF signal failed! Keep receiving...", recv_counter);

            if (ltoc_temp > min_sigpow_mul * usrp_noise_power)
            {
                ltoc_vec.emplace_back(ltoc_temp);
                LOG_INFO_FMT("Rx power of signal from cent = %1%", ltoc_temp);
                // publish
                mqttClient.publish(ctol_topic, mqttClient.timestamp_float_data(ltoc_temp), false);
            }
            else
            {
                LOG_WARN("Received Rx power of the signal is too low");
                reception_failed = true;
            }

            est_count++;
            csd_success_flag = false;
        }

        if (reception_failed)
            continue;

        float ltoc_mean = 0.0;
        for (auto val : ltoc_vec)
            ltoc_mean += val;
        ltoc = ltoc_mean / ltoc_vec.size();
        LOG_INFO_FMT("Average Rx power of signal from leaf = %1%", ltoc);
        mqttClient.publish(flag_topic_cent, mqttClient.timestamp_str_data("recv"), false);
    }

    calibration_ends = true;
}

void Calibration::consumer()
{
    while (not signal_stop_called)
    {
        csd_obj->consume(csd_success_flag, signal_stop_called);
        if (csd_success_flag)
            LOG_INFO("***Successful CSD!");
    }
}

bool Calibration::transmission(const float &scale)
{
    auto tx_ref_waveform = ref_waveform;
    if (scale != 1.0) // if not full_scale = 1.0, implement scaling of signal
    {
        for (auto &elem : tx_ref_waveform)
        {
            elem *= scale;
        }
    }
    // add small delay to transmission
    auto tx_time = usrp_obj->usrp->get_time_now() + uhd::time_spec_t(5e-3);

    if (usrp_obj->transmission(tx_ref_waveform, tx_time, signal_stop_called, true))
        return true;
    else
        return false;
}

bool Calibration::reception(float &rx_sig_pow)
{
    auto producer_wrapper = [this](const std::vector<std::complex<float>> &samples, const size_t &sample_size, const uhd::time_spec_t &sample_time)
    {
        csd_obj->produce(samples, sample_size, sample_time, signal_stop_called);

        if (csd_success_flag || retx_flag || end_flag)
            return true;
        else
            return false;
    };

    usrp_obj->reception(signal_stop_called, 0, 0, uhd::time_spec_t(0.0), false, producer_wrapper);

    if (!csd_success_flag)
    {
        LOG_WARN("Reception ended without CSD success! Skip this round and transmit again.");
        return false;
    }

    rx_sig_pow = csd_obj->est_ref_sig_pow;

    csd_obj->est_ref_sig_pow = 0.0;

    return true;
}

bool Calibration::calibrate_gains(MQTTClient &mqttClient)
{
    float new_tx_gain = toDecibel(ctol / (calib_sig_scale * calib_sig_scale), true) - toDecibel(ltoc / (calib_sig_scale * calib_sig_scale), true) + usrp_obj->tx_gain;
    float impl_tx_gain = std::ceil(new_tx_gain * 2) / 2; // tx gains are implemented in 0.5dB steps
    float remainder_gain = 0.0;
    // If TX gain has reached maximum, start by increasing RX gain of leaf
    if (impl_tx_gain > max_tx_gain)
    {
        float new_rx_gain = usrp_obj->rx_gain + (max_tx_gain - new_tx_gain);
        float impl_rx_gain = std::ceil(new_rx_gain);
        usrp_obj->set_rx_gain(impl_rx_gain);
        new_tx_gain = max_tx_gain;
        usrp_obj->set_tx_gain(new_tx_gain);
        // inform cent to restart transmission
        mqttClient.publish(flag_topic_leaf, mqttClient.timestamp_str_data("retx"), false);
        recv_success = false;
        return false; // break to again receive new values from cent
    }
    else
    {
        usrp_obj->set_tx_gain(impl_tx_gain);
        recv_success = false;
        remainder_gain = new_tx_gain - usrp_obj->tx_gain;
    }

    // sleep to let USRP setup gains
    std::this_thread::sleep_for(std::chrono::microseconds(size_t(20e3)));

    // Adjust scale based on remainder gain
    full_scale = fromDecibel(remainder_gain, false);
    LOG_INFO_FMT("Full scale value %1%", full_scale);
    if (full_scale > 1.0)
        full_scale = 1.0;
    mqttClient.publish(cal_scale_topic, mqttClient.timestamp_float_data(full_scale), true);

    return true;
}

void Calibration::on_calib_success(MQTTClient &mqttClient)
{
    mqttClient.publish(flag_topic_leaf, mqttClient.timestamp_str_data("end"), false);
    LOG_INFO_FMT("Last recived signal power C->L and L->C = %1% and %2%", ctol, ltoc);
    LOG_INFO_FMT("Calibrated Tx-Rx gain values = %1% dB, %2% dB -- and scale = %3%", usrp_obj->tx_gain, usrp_obj->rx_gain, full_scale);
    mqttClient.publish(tx_gain_topic, mqttClient.timestamp_float_data(usrp_obj->tx_gain), true);
    mqttClient.publish(rx_gain_topic, mqttClient.timestamp_float_data(usrp_obj->rx_gain), true);
    mqttClient.publish(full_scale_topic, mqttClient.timestamp_float_data(full_scale), true);
}

void Calibration::run_scaling_tests_leaf()
{
    LOG_INFO("-------------------- STARTING CALIBRATION PERFORMANCE TESTS ------------------------------");
    MQTTClient &mqttClient = MQTTClient::getInstance(device_id);
    size_t mc_round = 0;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dist(0.0, 1.0);

    std::string tele_scaling_topic = mqttClient.topics->getValue_str("tele-powcalib") + device_id;

    size_t tx_counter = 0;
    float mctest_pow = 0.0;
    size_t mc_round_temp;

    // Callback to listen for mctest result from cent
    auto callback_mctest = [&tx_counter, &mc_round, &mctest_pow](const std::string &payload)
    {
        try
        {
            json jsonData = json::parse(payload);

            if (jsonData.contains("value"))
            {
                std::string temp_mctest = jsonData["value"];
                mctest_pow = std::stof(temp_mctest);
                LOG_DEBUG_FMT("MQTT >> MCTEST received = %1%", mctest_pow);
                mc_round++;
            }
        }
        catch (const json::parse_error &e)
        {
            LOG_WARN_FMT("MQTT >> JSON parsing error : %1%", e.what());
            LOG_WARN_FMT("Incorrect JSON string = %1%", payload);
        }
    };

    mqttClient.setCallback(mctest_topic, callback_mctest, true);

    while (mc_round < max_mctest_rounds)
    {
        float mc_temp = dist(gen);
        mc_round_temp = mc_round;
        while (mc_round == mc_round_temp and tx_counter++ < 10)
        {
            LOG_DEBUG_FMT("MC Round %1% : transmitting signal of amplitude = %2%", mc_round, mc_temp);
            transmission(mc_temp / calib_sig_scale);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        tx_counter = 0;

        if (mctest_pow == 0.0)
        {
            LOG_WARN("No data received from cent.");
        }
        else
        {
            json tString;
            tString["tx_scale"] = mc_temp;
            tString["rx_pow"] = mctest_pow;
            tString["time"] = currentDateTime();
            mqttClient.publish(tele_scaling_topic, tString.dump(4), false);
            mctest_pow = 0.0;
        }
    }

    scaling_test_ends = true;
}

void Calibration::run_scaling_tests_cent()
{
    LOG_INFO("-------------------- STARTING GAIN CALIBRATION TESTS ------------------------------");
    MQTTClient &mqttClient = MQTTClient::getInstance(device_id);

    size_t mc_round = 0;
    bool receive_flag = false;
    end_flag = false;
    float mc_temp;
    while (mc_round++ < max_mctest_rounds)
    {
        size_t recv_counter = 0;
        mc_temp = 0.0;
        receive_flag = false;
        csd_success_flag = false;
        while (not receive_flag and recv_counter++ < 10)
        {
            receive_flag = reception(mc_temp);
            if (not receive_flag)
                LOG_WARN_FMT("Attempt %1% : Reception of REF signal failed! Keep receiving...", recv_counter);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (not receive_flag)
        {
            LOG_WARN("All attempts for Reception of REF signal failed! Restart reception.");
            continue;
        }
        else if (mc_temp == 0.0)
        {
            LOG_WARN("Estimated rx pow incorrect!");
            continue;
        }
        else
        {
            LOG_INFO_FMT("Received signal power = %1%", mc_temp);
            mqttClient.publish(mctest_topic, mqttClient.timestamp_float_data(mc_temp));
        }
    }

    scaling_test_ends = true;
}