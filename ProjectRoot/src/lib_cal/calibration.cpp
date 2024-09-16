#include "calibration.hpp"

Calibration::Calibration(
    USRP_class &usrp_obj_,
    ConfigParser &parser_,
    const std::string &device_id_,
    const std::string device_type_,
    bool &signal_stop_called_) : usrp_obj(usrp_obj_),
                                 parser(parser_),
                                 device_id(device_id_),
                                 device_type(device_type_),
                                 signal_stop_called(signal_stop_called_),
                                 csd_obj(nullptr),
                                 peak_det_obj(nullptr),
                                 ref_waveform(),
                                 rand_waveform()
{
    if (device_type_ == "cent")
        cent_id = device_id;
    else if (device_type_ == "leaf")
        leaf_id = device_id;

    num_samps_sync = parser.getValue_int("Ref-N-zfc") * parser.getValue_int("Ref-R-zfc") * 10;
    tx_rand_wait = size_t(parser.getValue_float("start-tx-wait-microsec") / 1e3);

    CFO_topic = "calibration/CFO/" + leaf_id;
    flag_topic = "calibration/flag/" + leaf_id;
    ctol_rxpow_topic = "calibration/ctol/" + leaf_id;
    ltoc_rxpow_topic = "calibration/ltoc/" + cent_id;
    calibrated_tx_gain_topic = "calibration/tx_gain/" + leaf_id;
    calibrated_rx_gain_topic = "calibration/rx_gain/" + leaf_id;
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
    peak_det_obj = std::make_unique<PeakDetectionClass>(parser, usrp_obj.init_noise_ampl);
};

void Calibration::initialize_csd_obj()
{
    size_t capacity = std::pow(2.0, parser.getValue_int("capacity-pow"));
    double rx_sample_duration_float = 1 / parser.getValue_float("rate");
    uhd::time_spec_t rx_sample_duration = uhd::time_spec_t(rx_sample_duration_float);
    csd_obj = std::make_unique<CycleStartDetector>(parser, capacity, rx_sample_duration, *peak_det_obj);
};

void Calibration::generate_waveform()
{
    WaveformGenerator wf_gen;
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    size_t wf_pad = size_t(parser.getValue_int("Ref-padding-mul") * N_zfc);

    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    ref_waveform = wf_gen.generate_waveform();

    wf_gen.initialize(wf_gen.UNIT_RAND, num_samps_sync, 1, 0, 0, 1, 1.0, 123);
    rand_waveform = wf_gen.generate_waveform();
}

bool Calibration::initialize()
{
    stop_flag = false;
    calibration_successful = false;
    try
    {
        initialize_peak_det_obj();
        initialize_csd_obj();
        generate_waveform();
        return true;
    }
    catch (std::exception &e)
    {
        LOG_WARN_FMT("SyncTest Initialization failed with ERROR: %1%", e.what());
        return false;
    }
};

void Calibration::run()
{
    std::string device_type = parser.getValue_str("device-type");
    if (device_type == "leaf")
        producer_thread = boost::thread(&Calibration::producer_leaf, this);
    else if (device_type == "cent")
        producer_thread = boost::thread(&Calibration::producer_cent, this);

    consumer_thread = boost::thread(&Calibration::consumer, this);
};

void Calibration::stop()
{
    stop_flag.store(true);
    signal_stop_called = true;

    boost::this_thread::sleep_for(boost::chrono::milliseconds(500));

    LOG_INFO("Deleting Calibration Class object!");
    delete this;
}

void Calibration::producer_leaf()
{
    // reception/producer params
    std::string client_id = leaf_id;
    MQTTClient &mqttClient = MQTTClient::getInstance(client_id);

    // This function is called by the receiver as a callback everytime a frame is received
    auto producer_wrapper = [this](const std::vector<std::complex<float>> &samples, const size_t &sample_size, const uhd::time_spec_t &sample_time)
    {
        csd_obj->produce(samples, sample_size, sample_time, signal_stop_called);

        if (stop_flag)
            return true;
        else
            return false;
    };

    // callback to update ltoc_rssi value
    std::function<bool(const float &, const float &)> proximity_check = [](const float &val1, const float &val2)
    {
        float tolerance = 5e-2;
        float dist_2norm = (val1 - val2) * (val1 - val2) / (val1 * val1);
        if (dist_2norm < tolerance)
            return true;
        else
            return false;
    };

    float ctol_rssi = 0.0, ltoc_rssi = 0.0;
    bool recv_success = false;

    std::function<void(const std::string &)> update_ltoc_rssi = [this, &ltoc_rssi, &ctol_rssi, &recv_success, &proximity_check](const std::string &payload)
    {
        try
        {
            json jsonData = json::parse(payload);
            if (jsonData.contains("value"))
            {
                ltoc_rssi = jsonData["value"].get<float>();
                LOG_DEBUG_FMT("MQTT >> LTOC RSSI received = %1%", ltoc_rssi);
                recv_success = true;
                if (proximity_check(ctol_rssi, ltoc_rssi))
                    calibration_successful = true;
            }
        }
        catch (const json::parse_error &e)
        {
            LOG_WARN_FMT("MQTT >> JSON parsing error : %1%", e.what());
        }
    };

    // subscribe to MQTT topic
    mqttClient.setCallback(ltoc_rxpow_topic, update_ltoc_rssi);

    size_t round = 1, max_num_tx_rounds = 10;

    while (not signal_stop_called)
    {
        LOG_INFO_FMT("-------------- Receiving Round %1% ------------", round);

        usrp_obj.reception(signal_stop_called, 0, 0, uhd::time_spec_t(0.0), false, producer_wrapper);

        if (signal_stop_called)
            break;

        // publish CFO value
        mqttClient.publish(CFO_topic, mqttClient.timestamp_float_data(csd_obj->cfo), true);

        // capture signal after a specific duration from the peak
        uhd::time_spec_t rx_timer = csd_obj->get_wait_time();
        auto rx_samples = usrp_obj.reception(signal_stop_called, num_samps_sync, 0.0, rx_timer, true);

        // Leaf process rx_samples to obtain RSS value
        float min_sigpow_ratio = 0.05;
        ctol_rssi = calc_signal_power(rx_samples, 0, 0, min_sigpow_ratio);
        if (ctol_rssi > 10 * usrp_obj.init_noise_ampl * usrp_obj.init_noise_ampl)
        {
            LOG_INFO_FMT("Rx power of signal from cent = %1%", ctol_rssi);
            mqttClient.publish(ctol_rxpow_topic, mqttClient.timestamp_float_data(ctol_rssi), false);
            mqttClient.publish(flag_topic, mqttClient.timestamp_str_data("recv"), false);
        }
        else
        {
            LOG_WARN("Received Rx power of the signal is too low");
            continue; // skip transmission and receive again
        }

        // In loop
        float leaf_tx_round = 1;
        while (not signal_stop_called && not calibration_successful && leaf_tx_round++ < max_num_tx_rounds)
        {
            // based on ratio between ctol and ltoc rssi values, update TX gain of leaf node
            if (ltoc_rssi > 0 && recv_success)
            {
                float new_tx_gain = toDecibel(ctol_rssi, true) - toDecibel(ltoc_rssi, true) + usrp_obj.tx_gain;
                // If TX gain has reached maximum, start by increasing RX gain of leaf
                if (new_tx_gain > max_tx_gain)
                {
                    float new_rx_gain = usrp_obj.rx_gain + (max_tx_gain - new_tx_gain);
                    new_tx_gain = max_tx_gain;
                    usrp_obj.set_rx_gain(new_rx_gain);
                    usrp_obj.set_tx_gain(new_tx_gain);
                    // inform cent to restart transmission
                    mqttClient.publish(flag_topic, mqttClient.timestamp_str_data("retx"), false);
                    recv_success = false;
                    break; // break to again receive new values from cent
                }
                else
                {
                    usrp_obj.set_tx_gain(new_tx_gain);
                    recv_success = false;
                }
            }

            // Transmit REF + full-scale signal
            LOG_INFO_FMT("-------------- Transmit Round %1% ------------", leaf_tx_round);
            usrp_obj.transmission(ref_waveform, uhd::time_spec_t(0.0), signal_stop_called, true);
            usrp_obj.transmission(rand_waveform, usrp_obj.usrp->get_time_now() + uhd::time_spec_t(tx_rand_wait / 1e3), signal_stop_called, true);

            boost::this_thread::sleep_for(boost::chrono::milliseconds(subseq_tx_wait));
        }

        if (calibration_successful)
        {
            mqttClient.publish(flag_topic, mqttClient.timestamp_str_data("end"), false);
            LOG_INFO_FMT("Calibrated Tx-Rx gain values = %1% dB, %2% dB", usrp_obj.tx_gain, usrp_obj.rx_gain);
            LOG_INFO_FMT("Last recived signal power C->L and L->C = %1% and %2%", ctol_rssi, ltoc_rssi);
            mqttClient.publish(calibrated_tx_gain_topic, mqttClient.timestamp_float_data(usrp_obj.tx_gain), true);
            mqttClient.publish(calibrated_rx_gain_topic, mqttClient.timestamp_float_data(usrp_obj.rx_gain), true);
            break;
        }

        // move to next round
        stop_flag = false;
    }
};

void Calibration::producer_cent()
{
    std::string client_id = cent_id;
    MQTTClient &mqttClient = MQTTClient::getInstance(client_id);
    bool recv_flag = false, retx_flag = false, end_flag = false;

    std::function<void(const std::string &)> detect_flags = [&recv_flag, &retx_flag, &end_flag](const std::string &payload)
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
        }
    };
    mqttClient.setCallback(flag_topic, detect_flags);

    // This function is called by the receiver as a callback everytime a frame is received
    auto producer_wrapper = [this, &retx_flag, &end_flag](const std::vector<std::complex<float>> &samples, const size_t &sample_size, const uhd::time_spec_t &sample_time)
    {
        csd_obj->produce(samples, sample_size, sample_time, signal_stop_called);

        if (stop_flag && not retx_flag && not end_flag)
            return true;
        else
            return false;
    };

    // reception/producer params
    size_t round = 1;
    float ltoc_rssi = 0.0;

    while (not signal_stop_called && not end_flag)
    {
        LOG_INFO_FMT("-------------- Transmit Round %1% ------------", round);

        // Transmit REF
        while (not recv_flag and not signal_stop_called)
        {
            usrp_obj.transmission(ref_waveform, uhd::time_spec_t(0.0), signal_stop_called, true);
            usrp_obj.transmission(rand_waveform, usrp_obj.usrp->get_time_now() + uhd::time_spec_t(tx_rand_wait / 1e3), signal_stop_called, true);
            boost::this_thread::sleep_for(boost::chrono::milliseconds(subseq_tx_wait));
        }

        if (signal_stop_called || end_flag)
            break;

        // MQTT message received -- start reception
        if (recv_flag)
            recv_flag = false;

        // start receiving REF signal
        usrp_obj.reception(signal_stop_called, 0, 0, uhd::time_spec_t(0.0), false, producer_wrapper);

        if (signal_stop_called || end_flag)
            break;

        // capture signal after a specific duration from the last sample
        auto rx_timer = csd_obj->get_wait_time();
        auto rx_samples = usrp_obj.reception(signal_stop_called, num_samps_sync, 0.0, rx_timer, true);
        // estimate signal strength
        float min_sigpow_ratio = 0.05;
        ltoc_rssi = calc_signal_power(rx_samples, 0, 0, min_sigpow_ratio);
        if (ltoc_rssi > 10 * usrp_obj.init_noise_ampl * usrp_obj.init_noise_ampl)
        {
            LOG_INFO_FMT("Rx power of signal from cent = %1%", ltoc_rssi);
            // publish
            mqttClient.publish(ltoc_rxpow_topic, mqttClient.timestamp_float_data(ltoc_rssi), false);
        }
        else
        {
            LOG_WARN("Received Rx power of the signal is too low");
            recv_flag = true; // skip transmission and receive again
        }
    }
}

void Calibration::consumer()
{
    while (not signal_stop_called)
    {
        csd_obj->consume(stop_flag, signal_stop_called);
        if (stop_flag)
        {
            LOG_INFO("***Successful CSD!");
        }
    }
};