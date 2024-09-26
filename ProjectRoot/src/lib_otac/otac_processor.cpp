#include "otac_processor.hpp"

// TODO: consumer-producer routine for Cent reception (add ZFC seq to detect reception)
// TODO: Validate OTAC performance for each pair of devices (calibrate)
// TODO: Adpatable (dmin, dmax)
// TODO: Non-linear power amplification control

OTAC_class::OTAC_class(
    USRP_class &usrp_obj_,
    ConfigParser &parser_,
    const std::string &device_id_,
    const std::string &device_type_,
    const float &otac_input_,
    const float &dmin_,
    const float &dmax_,
    const size_t &num_leafs_,
    bool &signal_stop_called_) : usrp_obj(&usrp_obj_),
                                 parser(parser_),
                                 device_id(device_id_),
                                 device_type(device_type_),
                                 otac_input(otac_input_),
                                 dmin(dmin_),
                                 dmax(dmax_),
                                 num_leafs(num_leafs_),
                                 signal_stop_called(signal_stop_called_),
                                 csd_obj(nullptr),
                                 peak_det_obj(nullptr) {};

OTAC_class::~OTAC_class()
{
    if (producer_thread.joinable())
        producer_thread.join();
    if (consumer_thread.joinable())
        consumer_thread.join();
}

void OTAC_class::stop()
{
    csd_success_flag.store(true);

    LOG_INFO("Deleting OTAC Class object!");
    delete this;
}

void OTAC_class::initialize_peak_det_obj()
{
    float noise_ampl = usrp_obj->init_noise_ampl;
    noise_power = std::norm(noise_ampl);
    peak_det_obj = std::make_unique<PeakDetectionClass>(parser, noise_ampl);
}

void OTAC_class::initialize_csd_obj()
{
    size_t capacity = std::pow(2.0, parser.getValue_int("capacity-pow"));
    min_e2e_pow = std::norm(parser.getValue_float("min-e2e-amp"));
    max_e2e_pow = std::norm(parser.getValue_float("max-e2e-amp"));
    double rx_sample_duration_float = 1 / parser.getValue_float("rate");
    uhd::time_spec_t rx_sample_duration = uhd::time_spec_t(rx_sample_duration_float);
    csd_obj = std::make_unique<CycleStartDetector>(parser, capacity, rx_sample_duration, *peak_det_obj);
}

void OTAC_class::get_mqtt_topics()
{
    MQTTClient &mqttClient = MQTTClient::getInstance(device_id); // for publishing data

    // populate topics
    tele_otac_topic = mqttClient.topics->getValue_str("tele-otac-perf") + device_id;
}

void OTAC_class::generate_waveform()
{
    WaveformGenerator wf_gen;
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc"); // extra long ref signal for stability
    size_t wf_pad = size_t(parser.getValue_int("Ref-padding-mul") * N_zfc);

    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    ref_waveform = wf_gen.generate_waveform();

    size_t otac_wf_len = parser.getValue_int("test-signal-len");
    wf_gen.initialize(wf_gen.UNIT_RAND, 2 * otac_wf_len, 1, 0, 2 * otac_wf_len, 1, 1.0, 1);
    otac_waveform = wf_gen.generate_waveform();

    wf_gen.initialize(wf_gen.UNIT_RAND, otac_wf_len, 1, 0, 0, 1, 1.0, 1);
    fs_waveform = wf_gen.generate_waveform();
}

bool OTAC_class::initialize()
{
    csd_success_flag = false;
    try
    {
        initialize_peak_det_obj();
        initialize_csd_obj();
        generate_waveform();
        get_mqtt_topics();

        // MQTTClient &mqttClient = MQTTClient::getInstance(device_id);
        // if (device_type == "leaf")
        // {
        //     mqttClient.setCallback(tele_otac_topic, [this](const std::string &payload)
        //                            { callback_detect_flags(payload); });
        // }

        if (not readDeviceConfig(device_id, "fullscale", full_scale))
            LOG_WARN("Failed to read full_scale config.");
        else
        {
            if (full_scale <= 0.0 or full_scale >= 1.0)
                full_scale = 1.0;
        }

        return true;
    }
    catch (std::exception &e)
    {
        LOG_WARN_FMT("Calibration Initialization failed with ERROR: %1%", e.what());
        return false;
    }
};

void OTAC_class::run_proto()
{
    if (device_type == "leaf")
    {
        producer_thread = boost::thread(&OTAC_class::producer_leaf_proto, this);
        consumer_thread = boost::thread(&OTAC_class::consumer_leaf_proto, this);
    }
    else if (device_type == "cent")
    {
        producer_thread = boost::thread(&OTAC_class::producer_cent_proto, this);
        consumer_thread = boost::thread(&OTAC_class::consumer_cent_proto, this);
    }
};

float OTAC_class::compute_nmse(const float &val1, const float &val2)
{
    return sqrt(std::norm((val1 - val2)) / std::norm(val1));
}

bool OTAC_class::check_ctol()
{
    float upper_bound = max_e2e_pow;
    float lower_bound = min_e2e_pow;

    LOG_DEBUG_FMT("CTOL = %1%, Allowed bounds = (%2%, %3%)", ctol, lower_bound, upper_bound);
    if (ctol > upper_bound)
    {
        // reduce the rx gain of leaf
        float new_rx_gain = usrp_obj->rx_gain - toDecibel(ctol / upper_bound, true);
        float impl_rx_gain = std::floor(new_rx_gain);
        usrp_obj->set_rx_gain(impl_rx_gain);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        float noise_power = usrp_obj->set_background_noise_power();
        csd_obj->peak_det_obj_ref.noise_ampl = std::sqrt(noise_power);
        return false;
    }
    else if (ctol < lower_bound)
    {
        // increase the rx gain of leaf
        float new_rx_gain = usrp_obj->rx_gain - toDecibel(ctol / lower_bound, true);
        float impl_rx_gain = std::ceil(new_rx_gain);
        usrp_obj->set_rx_gain(impl_rx_gain);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        float noise_power = usrp_obj->set_background_noise_power();
        csd_obj->peak_det_obj_ref.noise_ampl = std::sqrt(noise_power);
        return false;
    }
    else
        return true;
}

bool OTAC_class::otac_pre_processing(float &sig_scale)
{
    if (otac_input < dmin or otac_input > dmax)
    {
        LOG_WARN_FMT("OTAC input %1% is outside the allowed bounds (%2%, %3%).", otac_input, dmin, dmax);
        return false;
    }

    float sig_input_pow = (otac_input - dmin) / (dmax - dmin);
    if (sig_input_pow < 0.0 or sig_input_pow > 1.0)
    {
        LOG_WARN_FMT("Pre-processed OTAC signal scale %1% is outside allowed bounds (0.0, 1.0)", sig_input_pow);
        return false;
    }
    float post_scaling = std::min<float>(full_scale / std::sqrt(ctol / min_e2e_pow), 1.0);
    sig_scale = sqrt(sig_input_pow) * post_scaling;
    return true;
}

bool OTAC_class::otac_post_processing(const float &sig_power, float &out_scale)
{
    float out_val = sig_power - noise_power;
    out_val *= (dmax - dmin) / min_e2e_pow;
    out_val += dmin * num_leafs;

    if (out_val < dmin * num_leafs or out_val > dmax * num_leafs)
    {
        LOG_WARN_FMT("Post-processed OTAC output %1% is outside the permissible bounds (%2%, %3%)", out_val, dmin * num_leafs, dmax * num_leafs);
        return false;
    }

    out_scale = out_val;
    return true;
}

void OTAC_class::producer_leaf_proto()
{
    LOG_INFO("Implementing OTAC Protocol");
    MQTTClient &mqttClient = MQTTClient::getInstance(device_id);

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

            // check if ctol is too large or too small
            if (not check_ctol())
            {
                LOG_WARN("Adjust Rx gain and skip this transmission round!");
                continue;
            }

            // transmit otac signal
            if (tx_timer <= uhd::time_spec_t(0.0) or tx_timer > usrp_obj->usrp->get_time_now() + uhd::time_spec_t(0.1))
            {
                LOG_WARN_FMT("Estimated timer %1% secs from REF is incorrect. Skip transmission.", tx_timer.get_real_secs());
                continue;
            }
            float sig_scale = 0.0;
            if (not otac_pre_processing(sig_scale))
            {
                LOG_WARN("OTAC Pre-processing failed! Skip transmission.");
                continue;
            }

            LOG_DEBUG_FMT("Transmitting OTAC signal with scale %1%", sig_scale);
            bool tx_success = transmission_otac(sig_scale, tx_timer);
            if (not tx_success)
            {
                LOG_WARN("OTAC tranmission failed!");
            }
            else
            {
                LOG_DEBUG("OTAC transmission successful.");
            }
        }
    }

    otac_routine_ends = true;
};

void OTAC_class::producer_cent_proto()
{
    MQTTClient &mqttClient = MQTTClient::getInstance(device_id);

    // reception/producer params
    size_t round = 0;

    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    // size_t R_zfc = parser.getValue_int("Ref-R-zfc");
    size_t ref_pad_len = parser.getValue_int("Ref-padding-mul") * N_zfc;
    double first_sample_gap = ref_pad_len / usrp_obj->rx_rate;
    double wait_duration = first_sample_gap + (parser.getValue_float("start-tx-wait-microsec") / 1e6);
    size_t otac_wf_len = parser.getValue_int("test-signal-len");

    while (not signal_stop_called && round++ < max_total_round)
    {
        LOG_INFO_FMT("-------------- Round %1% ------------", round);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Transmit REF
        uhd::time_spec_t tx_timer = usrp_obj->usrp->get_time_now() + uhd::time_spec_t(5e-3);
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
                float exp_wait_time = parser.getValue_float("start-tx-wait-microsec");
                if (txrx_gap > exp_wait_time + 200)
                {
                    LOG_WARN("OTAC signal reception delay is too big -> Reject this data.");
                    continue;
                }

                float otac_output;
                if (not otac_post_processing(ltoc, otac_output))
                {
                    LOG_WARN_FMT("OTAC post-processing failed!");
                    continue;
                }
                otac_output_list.emplace_back(otac_output);
                float nmse_val = compute_nmse(otac_input, otac_output);
                nmse_list.emplace_back(nmse_val);
                LOG_INFO_FMT("OTAC output = %1%  -- NMSE = %2%", otac_output, nmse_val);
            }
            else
                LOG_WARN("Reception failed!");
        }
    }

    otac_routine_ends = true;
}

void OTAC_class::consumer_leaf_proto()
{
    while (not signal_stop_called)
    {
        csd_obj->consume(csd_success_flag, signal_stop_called);
        if (csd_success_flag)
            LOG_INFO("***Successful CSD!");
    }
}

void OTAC_class::consumer_cent_proto()
{
    while (not signal_stop_called)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool OTAC_class::transmission_ref(const float &scale, const uhd::time_spec_t &tx_timer)
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

bool OTAC_class::transmission_otac(const float &scale, const uhd::time_spec_t &tx_timer)
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

    std::vector<std::complex<float>> fs_tx_waveform = fs_waveform;
    correct_cfo_tx(fs_tx_waveform, 1.0, current_cfo, cfo_counter);

    tx_waveform.insert(tx_waveform.begin(), fs_tx_waveform.begin(), fs_tx_waveform.end());

    if (usrp_obj->transmission(tx_waveform, tx_timer, signal_stop_called, true))
        return true;
    else
        return false;
}

bool OTAC_class::reception_ref(float &rx_sig_pow, uhd::time_spec_t &tx_timer)
{
    auto producer_wrapper = [this](const std::vector<std::complex<float>> &samples, const size_t &sample_size, const uhd::time_spec_t &sample_time)
    {
        csd_obj->produce(samples, sample_size, sample_time, signal_stop_called);

        if (csd_success_flag)
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

bool OTAC_class::reception_otac(float &rx_sig_pow, uhd::time_spec_t &tx_timer)
{
    size_t otac_wf_len = parser.getValue_int("test-signal-len");
    size_t req_num_samps = 10 * otac_wf_len;
    auto otac_rx_samps = usrp_obj->reception(signal_stop_called, req_num_samps, 0.0, tx_timer, true);
    if (otac_rx_samps.size() == req_num_samps)
        return otac_signal_detection(otac_rx_samps, rx_sig_pow, tx_timer);
    else
        return false;
}

bool OTAC_class::otac_signal_detection(const std::vector<std::complex<float>> &signal, float &signal_power, uhd::time_spec_t &signal_start_timer, const size_t &type_id)
{
    if (type_id == 0) // looks for a full-scale signal over a window followed by a gap, and then signal with highest mean-square-norm over window
    {
        // size_t otac_wf_len = parser.getValue_int("test-signal-len");
        // size_t req_num_samps = signal.size();
        // std::vector<float> norm_samples(req_num_samps);
        // std::transform(signal.begin(), signal.end(), norm_samples.begin(), [](const std::complex<float> &c)
        //                { return std::norm(c); });
        signal_power = 0.1;
        return true;
    }
    else if (type_id == 1) // only looks for signal with highest mean-square-norm over a window
    {
        size_t otac_wf_len = parser.getValue_int("test-signal-len");
        size_t req_num_samps = signal.size();
        std::vector<float> norm_samples(req_num_samps);
        std::transform(signal.begin(), signal.end(), norm_samples.begin(), [](const std::complex<float> &c)
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

        if (max_val < 10 * noise_power)
        {
            LOG_WARN_FMT("Estimated OTAC signal power = %1% .. is too low!", max_val);
            return false;
        }

        signal_start_timer += uhd::time_spec_t(max_index / usrp_obj->rx_rate);
        signal_power = max_val;
        return true;
    }
    else
    {
        return false;
    }
}