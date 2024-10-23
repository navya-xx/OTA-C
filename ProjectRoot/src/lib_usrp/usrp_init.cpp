#include "usrp_init.hpp"
#include <uhd/cal/database.hpp>

USRP_init::USRP_init(const ConfigParser &parser) : parser(parser) {};

uhd::sensor_value_t USRP_init::get_sensor_fn_rx(const std::string &sensor_name, const size_t &channel)
{
    return usrp->get_rx_sensor(sensor_name, channel);
}

uhd::sensor_value_t USRP_init::get_sensor_fn_tx(const std::string &sensor_name, const size_t &channel)
{
    return usrp->get_tx_sensor(sensor_name, channel);
}

bool USRP_init::check_locked_sensor_rx(float setup_time)
{
    auto sensor_names = usrp->get_rx_sensor_names(0);
    std::string sensor_name = "lo_locked";
    if (std::find(sensor_names.begin(), sensor_names.end(), sensor_name) == sensor_names.end())
        return false;

    auto setup_timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(int64_t(setup_time * 1000));
    bool lock_detected = false;

    LOG_INTO_BUFFER_FMT("Waiting for \"%1%\":", sensor_name);

    while (true)
    {
        if (lock_detected and (std::chrono::steady_clock::now() > setup_timeout))
        {
            LOG_INTO_BUFFER(" locked.");
            LOG_FLUSH_INFO();
            break;
        }
        if (get_sensor_fn_rx(sensor_name, 0).to_bool())
        {
            LOG_INTO_BUFFER("+");
            lock_detected = true;
        }
        else
        {
            if (std::chrono::steady_clock::now() > setup_timeout)
            {
                LOG_INFO_FMT("timed out waiting for consecutive locks on sensor \"%1%\"", sensor_name);
            }
            LOG_INTO_BUFFER("_");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}

bool USRP_init::check_locked_sensor_tx(float setup_time)
{
    auto sensor_names = usrp->get_tx_sensor_names(0);
    std::string sensor_name = "lo_locked";
    if (std::find(sensor_names.begin(), sensor_names.end(), sensor_name) == sensor_names.end())
        return false;

    auto setup_timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(int64_t(setup_time * 1000));
    bool lock_detected = false;

    LOG_INTO_BUFFER_FMT("Waiting for \"%1%\":", sensor_name);

    while (true)
    {
        if (lock_detected and (std::chrono::steady_clock::now() > setup_timeout))
        {
            LOG_INTO_BUFFER(" locked.");
            LOG_FLUSH_INFO();
            break;
        }
        if (get_sensor_fn_tx(sensor_name, 0).to_bool())
        {
            LOG_INTO_BUFFER("+");
            lock_detected = true;
        }
        else
        {
            if (std::chrono::steady_clock::now() > setup_timeout)
            {
                LOG_INFO_FMT("timed out waiting for consecutive locks on sensor \"%1%\"", sensor_name);
            }
            LOG_INTO_BUFFER("_");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}

void USRP_init::initialize(bool perform_rxtx_tests)
{
    device_id = parser.getValue_str("device-id");
    external_ref = parser.getValue_str("external-clock-ref") == "true";

    if (!check_and_create_usrp_device())
    {
        LOG_ERROR("Failed to create USRP device. Exiting!");
        return;
    }

    configure_clock_source();
    set_device_parameters();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    check_locked_sensor_rx();
    check_locked_sensor_tx();

    setup_streamers();

    usrp->set_time_now(uhd::time_spec_t(0.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    current_temperature = get_device_temperature();
    LOG_INFO_FMT("Current temperature of device = %1% C.", current_temperature);

    // publish_usrp_data();

    LOG_INFO("--------- USRP initialization finished -----------------");
}

bool USRP_init::check_and_create_usrp_device()
{
    bool usrp_make_success = false;
    std::string args = "serial=" + device_id;

    try
    {
        usrp = uhd::usrp::multi_usrp::make(args);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        usrp_make_success = true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR_FMT("%1%", e.what());
    }

    return usrp_make_success;
}

void USRP_init::print_usrp_device_info()
{
    LOG_INFO_FMT("Initializing Device: %1%", usrp->get_pp_string());
}

std::pair<float, float> USRP_init::query_calibration_data()
{
    float rx_pow_ref_input = parser.getValue_float("rx-pow-ref");
    float tx_pow_ref_input = parser.getValue_float("tx-pow-ref");

    // Query RX calibration data
    auto rx_info = usrp->get_usrp_rx_info();
    std::string cal_dir = get_home_dir() + "/uhd/caldata/";
    std::string rx_ref_power_file = cal_dir + rx_info["rx_ref_power_key"] + "_" + rx_info["rx_ref_power_serial"] + ".json";

    auto retval = find_closest_gain(rx_ref_power_file, rx_pow_ref_input, carrier_freq);
    float rx_pow_ref_gain = retval.first;
    float rx_pow_ref_pow = retval.second;
    LOG_INFO_FMT("Rx Power ref | requested %1% dBm | implemented %2% dBm | at gain %3% dB", rx_pow_ref_input, rx_pow_ref_pow, rx_pow_ref_gain);

    // Query TX calibration data
    auto tx_info = usrp->get_usrp_tx_info();
    std::string tx_ref_power_file = cal_dir + tx_info["tx_ref_power_key"] + "_" + tx_info["tx_ref_power_serial"] + ".json";

    auto reretval = find_closest_gain(tx_ref_power_file, tx_pow_ref_input, carrier_freq);
    float tx_pow_ref_gain = reretval.first;
    float tx_pow_ref_pow = reretval.second;
    LOG_INFO_FMT("Tx Power ref | requested %1% dBm | implemented %2% dBm | at gain %3% dB", tx_pow_ref_input, tx_pow_ref_pow, tx_pow_ref_gain);

    return {rx_pow_ref_gain, tx_pow_ref_gain};
}

void USRP_init::configure_clock_source()
{
    if (external_ref)
    {
        usrp->set_clock_source("external");

        LOG_INFO("Now confirming lock on clock signals...");
        bool is_locked = false;
        auto end_time = std::chrono::steady_clock::now() + std::chrono::duration(std::chrono::milliseconds(1000));

        while ((is_locked = usrp->get_mboard_sensor("ref_locked", 0).to_bool()) == false and std::chrono::steady_clock::now() < end_time)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (is_locked == false)
        {
            LOG_WARN("ERROR: Unable to confirm clock signal locked on board");
        }
    }

    LOG_INFO_FMT("Clock and time sources set to : %1% and %2%.", usrp->get_clock_source(0), usrp->get_time_source(0));
}

void USRP_init::set_device_parameters()
{
    set_antenna();
    set_master_clock_rate();
    set_sample_rate();
    set_center_frequency();
    set_initial_gains();
    set_bandwidth();
    apply_additional_settings();
    log_device_parameters();
}

void USRP_init::set_antenna()
{
    // LOG_DEBUG("Setting Tx/Rx antenna.");
    usrp->set_tx_antenna("TX/RX");
    usrp->set_rx_antenna("TX/RX");
    // LOG_DEBUG_FMT("Actual Tx/Rx antenna: %1%, %2%.", usrp->get_tx_antenna(), usrp->get_rx_antenna());
}

void USRP_init::set_master_clock_rate()
{
    float master_clock_rate_input = parser.getValue_float("master-clock-rate");
    // LOG_DEBUG_FMT("Setting master clock rate at : %1% ...", master_clock_rate_input);
    usrp->set_master_clock_rate(master_clock_rate_input);
    master_clock_rate = usrp->get_master_clock_rate();
    // LOG_DEBUG_FMT("Actual master clock rate set = %1%", master_clock_rate_out);
}

void USRP_init::set_sample_rate()
{
    float rate = parser.getValue_float("rate");
    if (rate <= 0.0)
    {
        throw std::invalid_argument("Specify a valid sampling rate!");
    }

    // LOG_DEBUG_FMT("Setting Tx/Rx Rate: %1% Msps.", (rate / 1e6));
    usrp->set_tx_rate(rate);
    usrp->set_rx_rate(rate, 0); // Assuming channel 0
    tx_rate = usrp->get_tx_rate(0);
    rx_rate = usrp->get_rx_rate(0);
    // LOG_DEBUG_FMT("Actual Tx Sampling Rate :  %1%", (tx_rate / 1e6));
    // LOG_DEBUG_FMT("Actual Rx Sampling Rate : %1%", (rx_rate / 1e6));
}

void USRP_init::set_center_frequency()
{
    float freq = parser.getValue_float("freq");
    float lo_offset = parser.getValue_float("lo-offset");

    // LOG_DEBUG_FMT("Setting TX/RX Freq: %1% MHz...", (freq / 1e6));
    // LOG_DEBUG_FMT("Setting TX/RX LO Offset: %1% MHz...", (lo_offset / 1e6));

    uhd::tune_request_t tune_request(freq, lo_offset);
    usrp->set_rx_freq(tune_request, 0); // Assuming channel 0
    usrp->set_tx_freq(tune_request, 0);
    carrier_freq = usrp->get_rx_freq(0);
    // LOG_DEBUG_FMT("Actual Rx Freq: %1% MHz...", (usrp->get_rx_freq(0) / 1e6));
    // LOG_DEBUG_FMT("Actual Tx Freq: %1% MHz...", (usrp->get_tx_freq(0) / 1e6));
}

void USRP_init::set_initial_gains()
{
    float tx_gain_input = get_gain("tx", use_calib_gains);

    // LOG_DEBUG_FMT("Setting TX Gain: %1% dB...", tx_gain_input);
    usrp->set_tx_gain(tx_gain_input, 0); // Assuming channel 0
    tx_gain = usrp->get_tx_gain(0);
    // LOG_DEBUG_FMT("Actual Tx Gain: %1% dB...", tx_gain);

    // Rx-gain
    float rx_gain_input = get_gain("rx", use_calib_gains);

    // LOG_DEBUG_FMT("Setting RX Gain: %1% dB...", rx_gain_input);
    usrp->set_rx_gain(rx_gain_input, 0); // Assuming channel 0
    rx_gain = usrp->get_rx_gain(0);
    // LOG_DEBUG_FMT("Actual Rx Gain: %1% dB...", rx_gain);
};

float USRP_init::get_gain(const std::string &trans_type, const bool &get_calib_gains)
{
    std::string config_type;
    if (trans_type == "tx")
    {
        config_type = "tx-gain";
    }
    else if (trans_type == "rx")
    {
        config_type = "rx-gain";
    }
    else
        LOG_WARN_FMT("Incorrect `trans_type´ = %1%. Allowed values are \"tx\" or \"rx\".", trans_type);

    float gain_val;
    if (get_calib_gains)
    {
        // if (mqttClient.temporary_listen_for_last_value(temp, tx_gain_topic, 10, 30))
        if (readDeviceConfig(device_id, "calib-" + config_type, gain_val))
        {
            if (gain_val == 0.0)
                return get_gain(trans_type, false);
            else
                return gain_val;
        }
        else
            return get_gain(trans_type, false);
    }
    else
    {
        if (parser.getValue_str("gain-mgmt") == "gain")
        {
            gain_val = parser.getValue_float(config_type);
        }
        else if (parser.getValue_str("gain-mgmt") == "power")
        {
            auto retval = query_calibration_data();

            if (retval.second == -100.0)
                gain_val = parser.getValue_float(config_type);
            else
                gain_val = retval.second;
        }
        return gain_val;
    }
}

void USRP_init::set_tx_gain(const float &_tx_gain, const int &channel)
{
    // LOG_DEBUG_FMT("Setting TX Gain: %1% dB...", _tx_gain);
    usrp->set_tx_gain(_tx_gain, channel);
    tx_gain = usrp->get_tx_gain(channel);
    // LOG_DEBUG_FMT("Actual TX Gain: %1% dB...", tx_gain);
};

void USRP_init::set_rx_gain(const float &_rx_gain, const int &channel)
{
    // LOG_DEBUG_FMT("Setting RX Gain: %1% dB...", _rx_gain);
    usrp->set_rx_gain(_rx_gain, channel);
    rx_gain = usrp->get_rx_gain(channel);
    // LOG_DEBUG_FMT("Actual RX Gain: %1% dB...", rx_gain);
};

void USRP_init::set_bandwidth()
{
    float rx_bw_input = parser.getValue_float("rx-bw");
    if (rx_bw_input >= 0.0)
    {
        LOG_DEBUG_FMT("Setting RX Bandwidth: %1% MHz...", (rx_bw_input / 1e6));
        usrp->set_rx_bandwidth(rx_bw_input, 0); // Assuming channel 0
        rx_bw = usrp->get_rx_bandwidth(0);
        LOG_DEBUG_FMT("Actual Rx Bandwidth: %1% MHz...", (rx_bw / 1e6));
    }

    float tx_bw_input = parser.getValue_float("tx-bw");
    if (tx_bw_input >= 0.0)
    {
        LOG_DEBUG_FMT("Setting TX Bandwidth: %1% MHz...", (tx_bw_input / 1e6));
        usrp->set_tx_bandwidth(tx_bw_input, 0); // Assuming channel 0
        tx_bw = usrp->get_tx_bandwidth(0);
        LOG_DEBUG_FMT("Actual Tx Bandwidth: %1% MHz...", (tx_bw / 1e6));
    }

    std::this_thread::sleep_for(std::chrono::microseconds(500)); // Allow setup to settle
}

void USRP_init::apply_additional_settings()
{
    // This function could handle any additional settings, such as specific filters,
    // advanced tuning settings, or any hardware-specific features.
}

void USRP_init::log_device_parameters()
{
    LOG_DEBUG_FMT("Master Clock Rate: %1% Msps...", (master_clock_rate / 1e6));
    LOG_DEBUG_FMT("Actual Tx Sampling Rate :  %1%", (tx_rate / 1e6));
    LOG_DEBUG_FMT("Actual Rx Sampling Rate : %1%", (rx_rate / 1e6));
    LOG_DEBUG_FMT("Actual Rx Freq: %1% MHz...", (usrp->get_rx_freq(0) / 1e6));
    LOG_DEBUG_FMT("Actual Tx Freq: %1% MHz...", (usrp->get_tx_freq(0) / 1e6));
    LOG_DEBUG_FMT("Actual Rx Gain: %1% dB...", rx_gain);
    LOG_DEBUG_FMT("Actual Tx Gain: %1% dB...", tx_gain);
    LOG_DEBUG_FMT("Tx Ref gain levels: %1%...", usrp->get_tx_power_reference(0)); // Assuming channel 0
    LOG_DEBUG_FMT("Rx Ref gain levels: %1%...", usrp->get_rx_power_reference(0)); // Assuming channel 0
    LOG_DEBUG_FMT("Actual Rx Bandwidth: %1% MHz...", (rx_bw / 1e6));
    LOG_DEBUG_FMT("Actual Tx Bandwidth: %1% MHz...", (tx_bw / 1e6));
}

void USRP_init::print_available_sensors()
{
    for (auto sensor : usrp->get_mboard_sensor_names(0))
    {
        LOG_INFO_FMT("MBoard Sensor %1% -- avaiable", sensor);
    }

    for (auto sensor : usrp->get_tx_sensor_names(0))
    {
        LOG_INFO_FMT("Tx Sensor %1% -- avaiable", sensor);
    }

    for (auto sensor : usrp->get_rx_sensor_names(0))
    {
        LOG_INFO_FMT("Rx Sensor %1% -- avaiable", sensor);
    }
}

float USRP_init::get_device_temperature()
{
    try
    {
        uhd::sensor_value_t temp = usrp->get_tx_sensor("temp", 0);
        // std::cout << "USRP Device Temperature: " << temp.to_pp_string() << std::endl;
        return float(temp.to_real());
    }
    catch (const uhd::lookup_error &e)
    {
        LOG_WARN_FMT("Temperature sensor not found. Error : %1%...", e.what());
        return 0.0;
    }
}

void USRP_init::setup_streamers()
{
    std::string cpu_format = parser.getValue_str("cpu-format");
    std::string otw_format = parser.getValue_str("otw-format");
    uhd::stream_args_t stream_args(cpu_format, otw_format);
    std::vector<size_t> channel_nums = {0}; // Assuming channel 0
    stream_args.channels = channel_nums;
    rx_streamer = usrp->get_rx_stream(stream_args);
    tx_streamer = usrp->get_tx_stream(stream_args);

    max_rx_packet_size = rx_streamer->get_max_num_samps();
    max_tx_packet_size = tx_streamer->get_max_num_samps();

    rx_sample_duration = 1 / rx_rate;
    tx_sample_duration = 1 / tx_rate;
}

uhd::time_spec_t USRP_init::get_time_now()
{
    return usrp->get_time_now();
}

void USRP_init::update_cfo(const float &new_cfo)
{
    cfo += new_cfo;
    flag_correct_cfo = true;
    if (!saveDeviceConfig(device_id, "CFO", floatToStringWithPrecision(cfo, 8)))
        LOG_WARN_FMT("Failed to save CFO value %1% to config file.", floatToStringWithPrecision(cfo, 8));
}

bool USRP_init::get_last_cfo()
{
    if (readDeviceConfig(device_id, "CFO", cfo))
    {
        if (cfo == 0.0)
            return false;
        else
            return true;
    }
    else
        return false;
}