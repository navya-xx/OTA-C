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
    min_e2e_pow = std::norm(parser.getValue_float("min-e2e-amp"));
    max_e2e_pow = std::norm(parser.getValue_float("max-e2e-amp"));
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

    size_t otac_wf_len = parser.getValue_int("test-signal-len");
    wf_gen.initialize(wf_gen.UNIT_RAND, otac_wf_len, 1, 0, otac_wf_len, 1, calib_sig_scale, 1);
    otac_waveform = wf_gen.generate_waveform();
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
            if (not readDeviceConfig(device_id, "fullscale", full_scale))
                LOG_WARN("Failed to read full_scale config.");
            else
            {
                if (full_scale <= 0.0 or full_scale >= 1.0)
                    full_scale = 1.0;
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

void Calibration::run_proto1()
{
    // warm up the device
    for (int i = 0; i < 1; ++i)
    {
        usrp_obj->perform_tx_test();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (device_type == "leaf")
    {
        producer_thread = boost::thread(&Calibration::producer_leaf_proto1, this);
        consumer_thread = boost::thread(&Calibration::consumer_leaf_proto1, this);
    }
    else if (device_type == "cent")
    {
        producer_thread = boost::thread(&Calibration::producer_cent_proto1, this);
        consumer_thread = boost::thread(&Calibration::consumer_cent_proto1, this);
    }
};

void Calibration::run_proto2()
{
    // warm up the device
    for (int i = 0; i < 1; ++i)
    {
        usrp_obj->perform_tx_test();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (device_type == "leaf")
    {
        producer_thread = boost::thread(&Calibration::producer_leaf_proto2, this);
        consumer_thread = boost::thread(&Calibration::consumer_leaf_proto2, this);
    }
    else if (device_type == "cent")
    {
        producer_thread = boost::thread(&Calibration::producer_cent_proto2, this);
        consumer_thread = boost::thread(&Calibration::consumer_cent_proto2, this);
    }
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
    {
        producer_thread = boost::thread(&Calibration::run_scaling_tests_leaf, this);
        consumer_thread = boost::thread(&Calibration::consumer_leaf_proto1, this);
    }
    else if (device_type == "cent")
    {
        producer_thread = boost::thread(&Calibration::run_scaling_tests_cent, this);
        consumer_thread = boost::thread(&Calibration::consumer_cent_proto1, this);
    }
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
        return true;
    }
    else
    {
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
            if (ltoc < 0)
                LOG_WARN("CTOL is not updated yet!");
            // else if (proximity_check(ctol, ltoc))
            //     calibration_successful = true;
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

bool Calibration::check_ctol()
{
    float upper_bound = max_e2e_pow;
    float lower_bound = min_e2e_pow;
    MQTTClient &mqttClient = MQTTClient::getInstance(leaf_id);
    LOG_DEBUG_FMT("CTOL = %1%, Allowed bounds = (%2%, %3%)", ctol, lower_bound, upper_bound);
    if (ctol > upper_bound)
    {
        // reduce the rx gain of leaf
        mqttClient.publish(flag_topic_leaf, mqttClient.timestamp_str_data("retx"), false); // restart
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        float new_rx_gain = usrp_obj->rx_gain - toDecibel(ctol / upper_bound, true);
        float impl_rx_gain = std::floor(new_rx_gain);
        usrp_obj->set_rx_gain(impl_rx_gain);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        float noise_power = usrp_obj->estimate_background_noise_power();
        csd_obj->peak_det_obj_ref.noise_ampl = std::sqrt(noise_power);
        return false;
    }
    else if (ctol < lower_bound)
    {
        // increase the rx gain of leaf
        mqttClient.publish(flag_topic_leaf, mqttClient.timestamp_str_data("retx"), false); // restart
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        float new_rx_gain = usrp_obj->rx_gain - toDecibel(ctol / lower_bound, true);
        float impl_rx_gain = std::ceil(new_rx_gain);
        usrp_obj->set_rx_gain(impl_rx_gain);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        float noise_power = usrp_obj->estimate_background_noise_power();
        csd_obj->peak_det_obj_ref.noise_ampl = std::sqrt(noise_power);
        return false;
    }
    else
        return true;
}

void Calibration::producer_leaf_proto1()
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
        size_t est_count = 0;
        std::vector<float> ctol_vec;
        bool reception_failed = false;
        while (est_count < reps_total and not signal_stop_called)
        {
            float ctol_temp;
            uhd::time_spec_t tmp_timer;
            size_t recv_counter = 0;
            while (not reception_ref(ctol_temp, tmp_timer) and recv_counter++ < 10)
                LOG_WARN_FMT("Attempt %1% : Reception of REF signal failed! Keep receiving...", recv_counter);

            if (ctol_temp > min_sigpow_mul * usrp_noise_power)
            {
                ctol_vec.emplace_back(ctol_temp);
                LOG_INFO_FMT("Rx power of signal from cent = %1%", ctol_temp);
                // publish
                // mqttClient.publish(ctol_topic, mqttClient.timestamp_float_data(ctol_temp), false);
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
        mqttClient.publish(ctol_topic, mqttClient.timestamp_float_data(ctol), false);
        mqttClient.publish(flag_topic_leaf, mqttClient.timestamp_str_data("recv"), false);

        if (signal_stop_called)
            break;

        // Update Tx/Rx gains based on ltoc values obtained
        size_t leaf_tx_round = 1;
        recv_success = false;
        // Transmit scaled REF
        while (not signal_stop_called and not calibration_successful and not recv_success)
        {
            transmission_ref(full_scale);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // based on ratio between ctol and ltoc rssi values, update TX gain of leaf node
        if (ltoc > 0 && recv_success)
        {
            if (calibrate_gains(mqttClient))
            {
                ltoc = 0.0;
            }
            else
                LOG_WARN("Setting gains for calibration failed!"); // tx_gain adjustment alone is not sufficient -- break and restart with reception again
        }

        // increase proximity_tol to counter randomness
        proximity_tol = proximity_tol * std::max(1.0, std::ceil(double(round / 10)));
    }

    calibration_ends = true;
};

void Calibration::producer_cent_proto1()
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
            transmission_ref();
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
        size_t est_count = 0;
        std::vector<float> ltoc_vec;
        bool reception_failed = false;
        while (est_count < reps_total and not signal_stop_called)
        {
            float ltoc_temp;
            uhd::time_spec_t tmp_timer;
            size_t recv_counter = 0;
            while (not reception_ref(ltoc_temp, tmp_timer) and recv_counter++ < 10)
                LOG_WARN_FMT("Attempt %1% : Reception of REF signal failed! Keep receiving...", recv_counter);

            if (ltoc_temp > min_sigpow_mul * usrp_noise_power)
            {
                ltoc_vec.emplace_back(ltoc_temp);
                LOG_INFO_FMT("Rx power of signal from cent = %1%", ltoc_temp);
                // publish
                // mqttClient.publish(ctol_topic, mqttClient.timestamp_float_data(ltoc_temp), false);
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
        mqttClient.publish(ltoc_topic, mqttClient.timestamp_float_data(ltoc), false);
    }

    calibration_ends = true;
}

void Calibration::consumer_leaf_proto1()
{
    while (not signal_stop_called)
    {
        csd_obj->consume(csd_success_flag, signal_stop_called);
        if (csd_success_flag)
            LOG_INFO("***Successful CSD!");
    }
}

void Calibration::consumer_cent_proto1()
{
    while (not signal_stop_called)
    {
        csd_obj->consume(csd_success_flag, signal_stop_called);
        if (csd_success_flag)
            LOG_INFO("***Successful CSD!");
    }
}

void Calibration::producer_leaf_proto2()
{
    LOG_INFO("Implementing Calibration Protocol #2");
    MQTTClient &mqttClient = MQTTClient::getInstance(leaf_id);

    size_t round = 0;

    while (not signal_stop_called && round++ < max_total_round)
    {
        LOG_INFO_FMT("-------------- Receiving Round %1% ------------", round);

        // receive REF
        uhd::time_spec_t tx_timer;
        bool reception_successful = reception_ref(ctol, tx_timer);
        if (not reception_successful)
        {
            LOG_WARN("Reception failed! Try again...");
            continue;
        }
        else
        {
            csd_success_flag = false;
            if (ctol > 0.0)
                LOG_INFO_FMT("Reception successful with ctol = %1% and timer-gap = %2% millisecs", ctol, (tx_timer - usrp_obj->usrp->get_time_now()).get_real_secs() * 1e3);
            else
                continue;

            // // check if ctol is too large or too small
            // if (not check_ctol())
            // {
            //     LOG_WARN("Repeat ref reception with new Rx gain!");
            //     continue;
            // }

            // transmit otac signal
            recv_success = false;
            if (tx_timer <= uhd::time_spec_t(0.0))
            {
                LOG_WARN("Estimate REF timer incorrect. Transmitting OTAC signal without proper reference.");
                tx_timer = usrp_obj->usrp->get_time_now() + uhd::time_spec_t(parser.getValue_float("start-tx-wait-microsec") / 1e6);
            }

            float sig_scale = std::min<float>(full_scale / std::sqrt(ctol / min_e2e_pow), 10.0);
            LOG_DEBUG_FMT("Transmitting OTAC signal with scale %1% = (%2% * %3% / %4%)", full_scale / std::sqrt(ctol / min_e2e_pow), full_scale, std::sqrt(min_e2e_pow), std::sqrt(ctol));
            if (full_scale / std::sqrt(ctol / min_e2e_pow) > 10.0)
                LOG_DEBUG("Tx signal scale is clipped at 10.0");

            bool tx_success = transmission_otac(sig_scale, tx_timer);

            if (not tx_success)
            {
                LOG_WARN("OTAC tranmission failed!");
                mqttClient.publish(flag_topic_leaf, mqttClient.timestamp_str_data("retx"), false); // restart
                continue;
            }
            else
            {
                size_t wait_counter = 0;
                while (not recv_success && wait_counter++ < 20)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));

                if (recv_success)
                {
                    // calibrate gains
                    bool gains_calib_success = calibrate_gains(mqttClient);
                    if (not gains_calib_success)
                    {
                        LOG_WARN("Gains calibration failed.");
                        continue;
                    }
                }
                else
                {
                    LOG_WARN("No info received from cent about ltoc. Start again!");
                    mqttClient.publish(flag_topic_leaf, mqttClient.timestamp_str_data("retx"), false);
                }
            }
        }

        if (calibration_ends)
            break;

        proximity_tol = init_proximity_tol * std::max(1.0, std::ceil(double(round / 3)));
    }

    calibration_ends = true;
}

void Calibration::producer_cent_proto2()
{
    MQTTClient &mqttClient = MQTTClient::getInstance(device_id);

    // reception/producer params
    size_t round = 0;

    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t ref_pad_len = parser.getValue_int("Ref-padding-mul") * N_zfc;
    double first_sample_gap = ref_pad_len / usrp_obj->rx_rate;
    double wait_duration = first_sample_gap + (parser.getValue_float("start-tx-wait-microsec") / 1e6);
    size_t otac_wf_len = parser.getValue_int("test-signal-len");

    while (not signal_stop_called && not end_flag && round++ < max_total_round)
    {
        LOG_INFO_FMT("-------------- Round %1% ------------", round);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        if (retx_flag)
            retx_flag = false;

        // Transmit REF
        uhd::time_spec_t tx_timer = usrp_obj->usrp->get_time_now() + uhd::time_spec_t(10e-3);
        bool transmit_success = transmission_ref(1.0, tx_timer);
        if (transmit_success)
        {
            uhd::time_spec_t otac_timer = tx_timer + uhd::time_spec_t(wait_duration);
            bool rx_success = reception_otac(ltoc, otac_timer);
            if (rx_success)
            {
                float txrx_gap = (otac_timer - tx_timer - uhd::time_spec_t(wait_duration)).get_real_secs() * 1e6;
                txrx_gap -= ((otac_wf_len / usrp_obj->rx_rate) * 1e6);
                LOG_INFO_FMT("OTAC signal synchronization gap = %1% microsecs", txrx_gap);
                LOG_INFO_FMT("OTAC ltoc = %1%", ltoc / min_e2e_pow);
                float exp_wait_time = parser.getValue_float("start-tx-wait-microsec");
                if (txrx_gap > exp_wait_time + 200)
                {
                    LOG_WARN("OTAC signal reception delay is too big -> Reject this data.");
                    continue;
                }
                mqttClient.publish(ltoc_topic, mqttClient.timestamp_float_data(ltoc / min_e2e_pow), false);
            }
            else
                LOG_WARN("Reception failed!");
        }
    }

    calibration_ends = true;
}

void Calibration::consumer_leaf_proto2()
{
    while (not signal_stop_called)
    {
        csd_obj->consume(csd_success_flag, signal_stop_called);
        if (csd_success_flag)
            LOG_INFO("***Successful CSD!");
    }
}

void Calibration::consumer_cent_proto2()
{
    while (not signal_stop_called)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool Calibration::transmission_ref(const float &scale, const uhd::time_spec_t &tx_timer)
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
    uhd::time_spec_t tx_timer_set;
    if (tx_timer < usrp_obj->usrp->get_time_now())
        tx_timer_set = usrp_obj->usrp->get_time_now() + uhd::time_spec_t(5e-3);
    else
        tx_timer_set = tx_timer;

    if (usrp_obj->transmission(tx_ref_waveform, tx_timer_set, signal_stop_called, true))
        return true;
    else
        return false;
}

bool Calibration::transmission_otac(const float &scale, const uhd::time_spec_t &tx_timer)
{
    std::vector<std::complex<float>> tx_waveform = otac_waveform;
    float my_scale;
    if (scale > 1.0) // if not full_scale = 1.0, implement scaling of signal
        my_scale = 1.0;
    else
        my_scale = scale;

    size_t cfo_counter = 0;
    float current_cfo = csd_obj->cfo;
    correct_cfo_tx(tx_waveform, my_scale, current_cfo, cfo_counter);

    if (usrp_obj->transmission(tx_waveform, tx_timer, signal_stop_called, true))
        return true;
    else
        return false;
}

bool Calibration::reception_ref(float &rx_sig_pow, uhd::time_spec_t &tx_timer)
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
    tx_timer = csd_obj->csd_wait_timer;

    csd_obj->est_ref_sig_pow = 0.0;

    return true;
}

bool Calibration::reception_otac(float &rx_sig_pow, uhd::time_spec_t &tx_timer)
{
    float usrp_noise_power = usrp_obj->init_noise_ampl * usrp_obj->init_noise_ampl;
    size_t otac_wf_len = parser.getValue_int("test-signal-len");
    size_t req_num_samps = 5 * otac_wf_len;
    auto otac_rx_samps = usrp_obj->reception(signal_stop_called, req_num_samps, 0.0, tx_timer, true);

    if (otac_rx_samps.size() == req_num_samps)
    {
        std::vector<float> norm_samples(otac_rx_samps.size());
        std::transform(otac_rx_samps.begin(), otac_rx_samps.end(), norm_samples.begin(), [](const std::complex<float> &c)
                       { return std::norm(c); });
        // compute signal power over window
        float max_val = 0.0, temp_val = 0.0, win_pow = 0.0;
        size_t max_index = 0;
        for (size_t i = 0; i < req_num_samps - otac_wf_len; ++i)
        {
            if (i == 0)
            {
                for (size_t j = 0; j < otac_wf_len; ++j)
                {
                    temp_val += norm_samples[j];
                }
            }
            else
            {
                temp_val -= norm_samples[i - 1];
                temp_val += norm_samples[i + otac_wf_len - 1];
            }

            win_pow = temp_val / otac_wf_len;
            if (win_pow > max_val)
            {
                max_val = win_pow;
                max_index = i;
            }
        }

        if (max_val < 10 * usrp_noise_power)
        {
            LOG_WARN_FMT("Estimated OTAC signal power = %1% .. is too low!", max_val);
            return false;
        }

        tx_timer += uhd::time_spec_t(max_index / usrp_obj->rx_rate);
        rx_sig_pow = max_val;
        return true;
    }
    else
        return false;
}

bool Calibration::calibrate_gains(MQTTClient &mqttClient)
{
    // float new_tx_gain = toDecibel(ctol / (calib_sig_scale * calib_sig_scale), true) - toDecibel(ltoc / (calib_sig_scale * calib_sig_scale), true) + usrp_obj->tx_gain;
    float ltoc_sig_scale = ltoc / std::norm(calib_sig_scale * full_scale);
    // bool prox_check = proximity_check(1.0, ltoc_sig_scale);
    // if (prox_check)
    // {
    //     on_calib_success(mqttClient);
    //     calibration_ends = true;
    //     return true;
    // }
    float new_tx_gain = usrp_obj->tx_gain - toDecibel(ltoc_sig_scale, true);
    float impl_tx_gain = std::ceil(new_tx_gain * 2) / 2; // tx gains are implemented in 0.5dB steps
    LOG_DEBUG_FMT("Requested TX gain = %1% dB", impl_tx_gain);
    float remainder_gain = 0.0;
    // If TX gain has reached maximum, start by increasing RX gain of leaf
    if (impl_tx_gain > max_tx_gain)
    {
        // float new_rx_gain = usrp_obj->rx_gain + (max_tx_gain - new_tx_gain);
        // float impl_rx_gain = std::ceil(new_rx_gain);
        // usrp_obj->set_rx_gain(impl_rx_gain);
        // new_tx_gain = max_tx_gain;
        // usrp_obj->set_tx_gain(new_tx_gain);
        // inform cent to restart transmission
        LOG_WARN_FMT("Requested TX gain %1% is greater than maximum allowed gain %2%", impl_tx_gain, max_tx_gain);
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
    // mqttClient.publish(cal_scale_topic, mqttClient.timestamp_float_data(full_scale), true);

    return true;
}

void Calibration::on_calib_success(MQTTClient &mqttClient)
{
    mqttClient.publish(flag_topic_leaf, mqttClient.timestamp_str_data("end"), false);
    LOG_INFO_FMT("Last recived signal power C->L and L->C = %1% and %2%", ctol, ltoc);
    LOG_INFO_FMT("Calibrated Tx-Rx gain values = %1% dB, %2% dB -- and scale = %3%", usrp_obj->tx_gain, usrp_obj->rx_gain, full_scale);
    if (not saveDeviceConfig(device_id, "calib-tx-gain", usrp_obj->tx_gain))
        LOG_WARN("Saving config `calib-tx-gain´ failed!");
    if (not saveDeviceConfig(device_id, "calib-rx-gain", usrp_obj->rx_gain))
        LOG_WARN("Saving config `calib-rx-gain´ failed!");
    mqttClient.publish(tx_gain_topic, mqttClient.timestamp_float_data(usrp_obj->tx_gain), true);
    mqttClient.publish(rx_gain_topic, mqttClient.timestamp_float_data(usrp_obj->rx_gain), true);
    if (not saveDeviceConfig(device_id, "fullscale", full_scale))
        LOG_WARN("Saving config `full_scale´ failed!");
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
            transmission_ref(mc_temp / calib_sig_scale);
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
    uhd::time_spec_t tmp_timer;
    while (mc_round++ < max_mctest_rounds)
    {
        size_t recv_counter = 0;
        mc_temp = 0.0;
        receive_flag = false;
        csd_success_flag = false;
        while (not receive_flag and recv_counter++ < 10)
        {
            receive_flag = reception_ref(mc_temp, tmp_timer);
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