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
    num_samps_sync = usrp_obj->max_rx_packet_size * 50;
    tx_rand_wait_microsec = size_t(parser.getValue_float("start-tx-wait-microsec"));

    parser.set_value("Ref-R-zfc", "50", "int", "New R-zfc for calibration");
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
    flag_topic = mqttClient.topics->getValue_str("calib-flags") + leaf_id; // flags are set by the leaf
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
            mqttClient.setCallback(flag_topic, [this](const std::string &payload)
                                   { callback_detect_flags(payload); });
        }
        else if (device_type == "leaf")
        {
            mqttClient.setCallback(ltoc_topic, [this](const std::string &payload)
                                   { callback_update_ltoc(payload); });
            std::string temp;
            if (mqttClient.temporary_listen_for_last_value(temp, full_scale_topic, 10, 30))
            {
                full_scale = std::stof(temp);
            }
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

void Calibration::stop()
{
    csd_success_flag.store(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    LOG_INFO("Deleting Calibration Class object!");
    delete this;
}

// checks whether two values are close to each other, based on tolerance value set inside the function
bool Calibration::proximity_check(const float &val1, const float &val2)
{
    float dist_norm = sqrt((val1 - val2) * (val1 - val2) / (val1 * val1));
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

        float ctol_temp;
        size_t recv_counter = 0;
        while (not reception(ctol_temp) and recv_counter++ < 10)
            LOG_WARN_FMT("Attempt %1% : Reception of REF signal failed! Keep receiving...", recv_counter);

        if (ctol_temp == 0.0)
        {
            LOG_WARN("10 attempts for Reception of REF signal failed! Restart transmission.");
            continue;
        }
        else
            ctol = ctol_temp;

        if (ctol > min_sigpow_mul * usrp_noise_power)
        {
            LOG_INFO_FMT("Rx power of signal from cent = %1%", ctol);
            // publish
            mqttClient.publish(ctol_topic, mqttClient.timestamp_float_data(ctol), false);
        }
        else
        {
            LOG_WARN("Received Rx power of the signal is too low");
            csd_success_flag = false;
            continue; // skip transmission and move to CSD again
        }

        if (signal_stop_called)
            break;

        // publish CFO value
        mqttClient.publish(CFO_topic, mqttClient.timestamp_float_data(csd_obj->cfo), true);

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
            if (not transmission(full_scale))
                LOG_WARN("Transmission failed! Repeat...");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (calibration_successful)
        {
            on_calib_success(mqttClient);
            break;
        }

        // increase proximity_tol to counter randomness
        proximity_tol = proximity_tol * std::ceil(round / 10);

        // move to next round
        csd_success_flag = false;
    }

    if (calibration_successful)
        run_scaling_tests(mqttClient);

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
        while (not reception(ltoc_temp) and recv_counter++ < 10)
            LOG_WARN_FMT("Attempt %1% : Reception of REF signal failed! Keep receiving...", recv_counter);

        if (ltoc_temp == 0.0)
        {
            LOG_WARN("All attempts for Reception of REF signal failed! Restart transmission.");
            continue;
        }
        else
            ltoc = ltoc_temp;

        if (ltoc > min_sigpow_mul * usrp_noise_power)
        {
            LOG_INFO_FMT("Rx power of signal from leaf = %1%", ltoc);
            // publish
            mqttClient.publish(ltoc_topic, mqttClient.timestamp_float_data(ltoc), false);
        }
        else
        {
            LOG_WARN("Received Rx power of the signal is too low");
            recv_flag = true; // skip transmission and receive again
        }
    }

    // on calib success -- start receiving signal for power meter test
    if (end_flag)
    {
        size_t mc_round = 0;
        while (mc_round++ < max_mctest_rounds)
        {
            size_t recv_counter = 0;
            float mc_temp = 0.0;
            while (not reception(mc_temp) and recv_counter++ < 10)
                LOG_WARN_FMT("Attempt %1% : Reception of REF signal failed! Keep receiving...", recv_counter);
            if (mc_temp == 0.0)
            {
                LOG_WARN("All attempts for Reception of REF signal failed! Restart transmission.");
                continue;
            }
            else
                mqttClient.publish(mctest_topic, mqttClient.timestamp_float_data(mc_temp));
        }
    }

    calibration_ends = true;
}

void Calibration::consumer()
{
    while (not signal_stop_called)
    {
        csd_obj->consume(csd_success_flag, signal_stop_called);
        if (csd_success_flag)
        {
            LOG_INFO("***Successful CSD!");
        }
    }
};

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

    // This function is called by the receiver as a callback everytime a frame is received
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

    return true;
}

bool Calibration::calibrate_gains(MQTTClient &mqttClient)
{
    float new_tx_gain = toDecibel(ctol / (calib_sig_scale * calib_sig_scale), true) - toDecibel(ltoc / (calib_sig_scale * calib_sig_scale), true) + usrp_obj->tx_gain;
    float impl_tx_gain = std::ceil(new_tx_gain * 2) / 2;
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
        mqttClient.publish(flag_topic, mqttClient.timestamp_str_data("retx"), false);
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
    mqttClient.publish(flag_topic, mqttClient.timestamp_str_data("end"), false);
    LOG_INFO_FMT("Last recived signal power C->L and L->C = %1% and %2%", ctol, ltoc);
    LOG_INFO_FMT("Calibrated Tx-Rx gain values = %1% dB, %2% dB -- and scale = %3%", usrp_obj->tx_gain, usrp_obj->rx_gain, full_scale);
    mqttClient.publish(tx_gain_topic, mqttClient.timestamp_float_data(usrp_obj->tx_gain), true);
    mqttClient.publish(rx_gain_topic, mqttClient.timestamp_float_data(usrp_obj->rx_gain), true);
    mqttClient.publish(full_scale_topic, mqttClient.timestamp_float_data(full_scale), true);
}

void Calibration::run_scaling_tests(MQTTClient &mqttClient)
{
    size_t mc_round = 0;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dist(0.0, 1.0);

    std::string tele_scaling_topic = mqttClient.topics->getValue_str("tele-powcalib");

    size_t tx_counter = 0;
    float mctest_pow = 0.0;

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

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    while (mc_round < max_mctest_rounds)
    {
        float mc_temp = dist(gen);
        size_t mc_round_temp = mc_round;
        LOG_DEBUG_FMT("MC Round %1% : Starting POW_CALIB test with signal of amplitude = %2%", mc_round, mc_temp);
        while (mc_round == mc_round_temp and tx_counter++ < 10)
        {
            transmission(mc_temp / calib_sig_scale);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

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
}