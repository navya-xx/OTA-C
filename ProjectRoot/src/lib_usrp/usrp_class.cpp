#include "usrp_class.hpp"
#include <uhd/cal/database.hpp>

USRP_class::USRP_class(const ConfigParser &parser) : parser(parser) {};

uhd::sensor_value_t USRP_class::get_sensor_fn_rx(const std::string &sensor_name, const size_t &channel)
{
    return usrp->get_rx_sensor(sensor_name, channel);
}

uhd::sensor_value_t USRP_class::get_sensor_fn_tx(const std::string &sensor_name, const size_t &channel)
{
    return usrp->get_tx_sensor(sensor_name, channel);
}

bool USRP_class::check_locked_sensor_rx(float setup_time)
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

bool USRP_class::check_locked_sensor_tx(float setup_time)
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

void USRP_class::initialize(bool perform_rxtx_tests)
{
    device_id = parser.getValue_str("device-id");
    external_ref = parser.getValue_str("external-clock-ref") == "true";

    if (!check_and_create_usrp_device())
    {
        LOG_ERROR("Failed to create USRP device. Exiting!");
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    configure_clock_source();
    set_device_parameters();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    check_locked_sensor_rx();
    check_locked_sensor_tx();

    setup_streamers();

    float noise_power = estimate_background_noise_power();
    init_noise_ampl = std::sqrt(noise_power);
    LOG_DEBUG_FMT("Average background noise for packets = %1%.", init_noise_ampl);

    // if (perform_rxtx_tests)
    // {
    //     perform_rx_test(); // for estimating background noise
    // }

    usrp->set_time_now(uhd::time_spec_t(0.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    current_temperature = get_device_temperature();
    LOG_INFO_FMT("Current temperature of device = %1% C.", current_temperature);

    publish_usrp_data();

    LOG_INFO("--------- USRP initialization finished -----------------");
}

bool USRP_class::check_and_create_usrp_device()
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

void USRP_class::print_usrp_device_info()
{
    LOG_INFO_FMT("Initializing Device: %1%", usrp->get_pp_string());
}

std::pair<float, float> USRP_class::query_calibration_data()
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

void USRP_class::configure_clock_source()
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

void USRP_class::set_device_parameters()
{
    set_antenna();
    set_master_clock_rate();
    set_sample_rate();
    set_center_frequency();
    set_initial_gains();
    set_bandwidth();
    apply_additional_settings();
}

void USRP_class::set_antenna()
{
    LOG_DEBUG("Setting Tx/Rx antenna.");
    usrp->set_tx_antenna("TX/RX");
    usrp->set_rx_antenna("TX/RX");
    LOG_DEBUG_FMT("Actual Tx/Rx antenna: %1%, %2%.", usrp->get_tx_antenna(), usrp->get_rx_antenna());
}

void USRP_class::set_master_clock_rate()
{
    float master_clock_rate_input = parser.getValue_float("master-clock-rate");
    LOG_DEBUG_FMT("Setting master clock rate at : %1% ...", master_clock_rate_input);
    usrp->set_master_clock_rate(master_clock_rate_input);
    float master_clock_rate_out = usrp->get_master_clock_rate();
    LOG_DEBUG_FMT("Actual master clock rate set = %1%", master_clock_rate_out);
}

void USRP_class::set_sample_rate()
{
    float rate = parser.getValue_float("rate");
    if (rate <= 0.0)
    {
        throw std::invalid_argument("Specify a valid sampling rate!");
    }

    LOG_DEBUG_FMT("Setting Tx/Rx Rate: %1% Msps.", (rate / 1e6));
    usrp->set_tx_rate(rate);
    usrp->set_rx_rate(rate, 0); // Assuming channel 0
    tx_rate = usrp->get_tx_rate(0);
    rx_rate = usrp->get_rx_rate(0);
    LOG_DEBUG_FMT("Actual Tx Sampling Rate :  %1%", (tx_rate / 1e6));
    LOG_DEBUG_FMT("Actual Rx Sampling Rate : %1%", (rx_rate / 1e6));
}

void USRP_class::set_center_frequency()
{
    float freq = parser.getValue_float("freq");
    float lo_offset = parser.getValue_float("lo-offset");

    LOG_DEBUG_FMT("Setting TX/RX Freq: %1% MHz...", (freq / 1e6));
    LOG_DEBUG_FMT("Setting TX/RX LO Offset: %1% MHz...", (lo_offset / 1e6));

    uhd::tune_request_t tune_request(freq, lo_offset);
    usrp->set_rx_freq(tune_request, 0); // Assuming channel 0
    usrp->set_tx_freq(tune_request, 0);
    carrier_freq = usrp->get_rx_freq(0);
    LOG_DEBUG_FMT("Actual Rx Freq: %1% MHz...", (usrp->get_rx_freq(0) / 1e6));
    LOG_DEBUG_FMT("Actual Tx Freq: %1% MHz...", (usrp->get_tx_freq(0) / 1e6));
}

void USRP_class::set_initial_gains()
{
    float tx_gain_input, rx_gain_input;

    // Tx gains
    // check if calibrated gains are available
    MQTTClient &mqttClient = MQTTClient::getInstance(device_id);
    std::string tx_gain_topic = mqttClient.topics->getValue_str("tx-gain") + device_id;
    std::string temp = "";
    if (use_calib_gains)
    {
        if (mqttClient.temporary_listen_for_last_value(temp, tx_gain_topic, 10, 30))
        {
            LOG_INFO("Using Calibrated Tx Gain Value.");
            tx_gain_input = std::stof(temp);
        }
    }
    else
    {
        if (parser.getValue_str("gain-mgmt") == "gain")
            tx_gain_input = parser.getValue_float("tx-gain");
        else if (parser.getValue_str("gain-mgmt") == "power")
        {
            auto retval = query_calibration_data();

            if (retval.second == -100.0)
                tx_gain_input = parser.getValue_float("tx-gain");
            else
                tx_gain_input = retval.second;
        }
    }

    LOG_DEBUG_FMT("Setting TX Gain: %1% dB...", tx_gain_input);
    usrp->set_tx_gain(tx_gain_input, 0); // Assuming channel 0
    tx_gain = usrp->get_tx_gain(0);
    LOG_DEBUG_FMT("Actual Tx Gain: %1% dB...", tx_gain);

    // Rx-gain
    std::string rx_gain_topic = mqttClient.topics->getValue_str("rx-gain") + device_id;
    temp = "";
    if (use_calib_gains)
    {
        if (mqttClient.temporary_listen_for_last_value(temp, rx_gain_topic, 10, 30))
        {
            LOG_INFO("Using Calibrated Rx Gain Values.");
            rx_gain_input = std::stof(temp);
        }
    }
    else
    {
        if (parser.getValue_str("gain-mgmt") == "gain")
            rx_gain_input = parser.getValue_float("rx-gain");
        else if (parser.getValue_str("gain-mgmt") == "power")
        {
            auto retval = query_calibration_data();

            if (retval.first == -100.0)
                rx_gain_input = parser.getValue_float("rx-gain");
            else
                rx_gain_input = retval.first;
        }
    }

    LOG_DEBUG_FMT("Setting RX Gain: %1% dB...", rx_gain_input);
    usrp->set_rx_gain(rx_gain_input, 0); // Assuming channel 0
    rx_gain = usrp->get_rx_gain(0);
    LOG_DEBUG_FMT("Actual Rx Gain: %1% dB...", rx_gain);
};

void USRP_class::set_tx_gain(const float &_tx_gain, const int &channel)
{
    LOG_DEBUG_FMT("Setting TX Gain: %1% dB...", _tx_gain);
    usrp->set_tx_gain(_tx_gain, channel);
    tx_gain = usrp->get_tx_gain(channel);
    LOG_DEBUG_FMT("Actual TX Gain: %1% dB...", tx_gain);
};

void USRP_class::set_rx_gain(const float &_rx_gain, const int &channel)
{
    LOG_DEBUG_FMT("Setting RX Gain: %1% dB...", _rx_gain);
    usrp->set_rx_gain(_rx_gain, channel);
    rx_gain = usrp->get_rx_gain(channel);
    LOG_DEBUG_FMT("Actual RX Gain: %1% dB...", rx_gain);
};

void USRP_class::set_bandwidth()
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

void USRP_class::apply_additional_settings()
{
    // This function could handle any additional settings, such as specific filters,
    // advanced tuning settings, or any hardware-specific features.
}

void USRP_class::log_device_parameters()
{
    LOG_DEBUG_FMT("Master Clock Rate: %1% Msps...", (usrp->get_master_clock_rate() / 1e6));
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

void USRP_class::print_available_sensors()
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

float USRP_class::get_device_temperature()
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

void USRP_class::setup_streamers()
{
    std::string cpu_format = parser.getValue_str("cpu-format");
    std::string otw_format = "sc16";
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

void USRP_class::perform_tx_test()
{
    std::vector<std::complex<float>> tx_buff(100 * max_tx_packet_size, std::complex<float>(1.0, 1.0));
    bool dont_stop = false;
    if (transmission(tx_buff, uhd::time_spec_t(0.0), dont_stop, true))
    {
        LOG_DEBUG("Test Tx -- success");
    }
    else
    {
        LOG_DEBUG("Test Tx -- failed");
    }
}

void USRP_class::perform_rx_test()
{
    bool dont_stop = false;

    size_t num_pkts = 100;
    auto rx_samples = reception(dont_stop, max_rx_packet_size * num_pkts);
    if (rx_samples.size() == max_rx_packet_size * num_pkts)
    {
        LOG_DEBUG_FMT("Reception test successful! Total %1% samples received.", rx_samples.size());
    }
    else
    {
        LOG_WARN("Reception test Failed!");
    }

    float noise_power = calc_signal_power(rx_samples);
    init_noise_ampl = std::sqrt(noise_power);
    LOG_DEBUG_FMT("Average background noise for packets = %1%.", init_noise_ampl);
}

float USRP_class::estimate_background_noise_power()
{
    bool dont_stop = false;

    size_t num_pkts = 50;
    auto rx_samples = reception(dont_stop, max_rx_packet_size * num_pkts);
    if (rx_samples.size() == max_rx_packet_size * num_pkts)
    {
        LOG_DEBUG_FMT("Reception successful! Total %1% samples received.", rx_samples.size());
    }
    else
    {
        LOG_WARN("Reception test Failed!");
    }

    return calc_signal_power(rx_samples);
}

void USRP_class::publish_usrp_data()
{
    json json_data;
    json_data["device_id"] = device_id;
    json_data["rx-gain"] = rx_gain;
    json_data["tx-gain"] = tx_gain;
    json_data["rx-rate"] = rx_rate;
    json_data["tx-rate"] = tx_rate;
    json_data["temp"] = current_temperature;
    json_data["noise-level"] = init_noise_ampl;
    json_data["time"] = currentDateTime();
    MQTTClient &mqttClient = MQTTClient::getInstance(device_id);
    std::string topic_init = mqttClient.topics->getValue_str("init-config") + device_id;
    mqttClient.publish(topic_init, json_data.dump(4), true);
}

bool USRP_class::transmission(const std::vector<std::complex<float>> &buff, const uhd::time_spec_t &tx_time, bool &stop_signal_called, bool ask_ack)
{
    bool success = false;
    size_t total_num_samps = buff.size();

    // setup metadata for the first packet
    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst = false;

    auto usrp_now = usrp->get_time_now();
    double time_diff = (tx_time - usrp_now).get_real_secs();
    LOG_DEBUG_FMT("TX with delay = %.4f microsecs.", (time_diff * 1e6));
    if (time_diff <= 0.0)
    {
        LOG_DEBUG_FMT("Transmitting %d samples WITHOUT delay.", total_num_samps);
        md.has_time_spec = false;
    }
    else
    {
        LOG_DEBUG_FMT("Transmitting %d samples WITH delay.", total_num_samps);
        md.has_time_spec = true;
        md.time_spec = tx_time;
    }

    const double burst_pkt_time = std::max<double>(0.1, (2.0 * max_tx_packet_size / tx_rate));
    double tx_delay, timeout;

    // transmission
    size_t num_acc_samps = 0;
    size_t num_tx_samps_sent_now = 0;
    bool transmit_failure = false;

    while (num_acc_samps < total_num_samps and not stop_signal_called)
    {
        size_t retry_tx_counter = 0;
        size_t samps_to_send = std::min(total_num_samps - num_acc_samps, max_tx_packet_size);

        while (true)
        {
            tx_delay = md.has_time_spec ? time_diff : 0.0;
            timeout = burst_pkt_time + tx_delay;
            try
            {
                num_tx_samps_sent_now = tx_streamer->send(&buff.front() + num_acc_samps, samps_to_send, md, timeout);
                if (num_tx_samps_sent_now < samps_to_send)
                {
                    LOG_WARN_FMT("TX-TIMEOUT! Actual num samples sent = %d, asked for = %d.", num_tx_samps_sent_now, samps_to_send);

                    // tx_streamer.reset();

                    ++retry_tx_counter;
                    if (retry_tx_counter > 5)
                    {
                        LOG_WARN_FMT("All %1% retries failed!", retry_tx_counter);
                        transmit_failure = true;
                        break;
                    }
                    else
                    {
                        LOG_WARN_FMT("Retry %1% to transmit signal again!", retry_tx_counter);
                        time_diff = (tx_time - usrp->get_time_now()).get_real_secs();
                        if (time_diff <= 0.0)
                            md.has_time_spec = false;
                        else
                        {
                            md.has_time_spec = true;
                            md.time_spec = tx_time;
                        }
                    }
                }
                else
                {
                    md.has_time_spec = false;
                    break;
                }
            }
            catch (const std::exception &e)
            {
                ++retry_tx_counter;
                LOG_WARN_FMT("Error in transmission in usrp_class.cpp::tx_streamer->send(...) : %1%", e.what());
                if (retry_tx_counter > 5)
                {
                    LOG_WARN_FMT("All %1% retries failed!", retry_tx_counter);
                    transmit_failure = true;
                    break;
                }
                continue;
            }
        }

        md.start_of_burst = false;
        num_acc_samps += num_tx_samps_sent_now;
    }

    if (transmit_failure)
        return false;

    // send a mini EOB packet
    md.end_of_burst = true;
    tx_streamer->send("", 0, md);

    if (ask_ack)
    {
        LOG_INTO_BUFFER("Waiting for async burst ACK... ");

        uhd::async_metadata_t async_md;
        bool got_async_burst_ack = false;
        float total_tx_time = std::max<float>(0.1, (total_num_samps / tx_rate));
        if (time_diff >= 0.0)
            timeout = total_tx_time + time_diff;
        else
            timeout = total_tx_time;
        // loop through all messages for the ACK packet (may have underflow messages in queue)
        while (not got_async_burst_ack and tx_streamer->recv_async_msg(async_md, timeout))
            got_async_burst_ack = (async_md.event_code == uhd::async_metadata_t::EVENT_CODE_BURST_ACK);

        LOG_INTO_BUFFER(got_async_burst_ack ? "success" : "fail");

        LOG_FLUSH_INFO();

        if (not got_async_burst_ack)
        {
            LOG_WARN("ACK FAIL..!");
        }
        else
            success = true;
    }
    else
    {
        if (num_acc_samps >= total_num_samps)
            success = true;
        else
        {
            LOG_WARN("Transmission FAILED..!");
        }
    }

    return success;
};

void USRP_class::continuous_transmission(const std::vector<std::complex<float>> &buff, std::atomic_bool &stop_transmission)
{

    // setup metadata for the first packet
    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst = false;

    const double burst_pkt_time = std::max<double>(0.1, (2.0 * max_tx_packet_size / tx_rate));

    // transmission
    size_t num_acc_samps = 0;
    size_t num_tx_samps_sent_now = 0;
    size_t total_num_samps = buff.size();

    while (not stop_transmission)
    {
        while (num_acc_samps < total_num_samps)
        {
            size_t samps_to_send = std::min(total_num_samps - num_acc_samps, max_tx_packet_size);
            num_tx_samps_sent_now = tx_streamer->send(&buff.front(), samps_to_send, md, burst_pkt_time);
            if (num_tx_samps_sent_now < samps_to_send)
            {
                LOG_WARN_FMT("TX-TIMEOUT! Actual num samples sent = %d, asked for = %d.", num_tx_samps_sent_now, buff.size());
            }

            if (stop_transmission)
                break;

            md.start_of_burst = false;
        }
        num_acc_samps = 0;
    }

    // send a mini EOB packet
    md.end_of_burst = true;
    tx_streamer->send("", 0, md);
};

std::vector<std::complex<float>> USRP_class::reception(bool &stop_signal_called, const size_t &req_num_rx_samps, const float &duration, const uhd::time_spec_t &rx_time, bool is_save_to_file, const std::function<bool(const std::vector<std::complex<float>> &, const size_t &, const uhd::time_spec_t &)> &callback)
{
    std::string filename;

    if (is_save_to_file)
    {
        if (not rx_save_stream.is_open())
        {
            std::string homeDirStr = get_home_dir();
            std::string curr_datetime = currentDateTimeFilename();
            filename = homeDirStr + "/OTA-C/ProjectRoot/storage/rx_saved_file_" + parser.getValue_str("device-id") + "_" + curr_datetime + ".dat";
        }
    }

    bool success = true;

    // setup streaming
    uhd::stream_cmd_t stream_cmd(req_num_rx_samps > max_rx_packet_size or req_num_rx_samps == 0 ? uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS : uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);

    stream_cmd.num_samps = req_num_rx_samps == 0 ? max_rx_packet_size : req_num_rx_samps;

    auto usrp_now = usrp->get_time_now();
    float time_diff = (rx_time - usrp_now).get_real_secs();

    if (rx_time <= usrp_now or rx_time == uhd::time_spec_t(0.0))
    {
        LOG_DEBUG("Receiving WITHOUT delay.");
        stream_cmd.stream_now = true;
    }
    else
    {
        LOG_DEBUG("Receiving WITH delay.");
        LOG_DEBUG_FMT("Rx delay : %.4f microsecs", (time_diff * 1e6));
        stream_cmd.stream_now = false;
        stream_cmd.time_spec = rx_time;
    }

    rx_streamer->issue_stream_cmd(stream_cmd);

    const double burst_pkt_time = std::max<double>(0.1, (2.0 * max_rx_packet_size / rx_rate));
    double rx_delay = stream_cmd.stream_now ? 0.0 : time_diff;
    double timeout = burst_pkt_time + rx_delay;

    std::vector<std::complex<float>> rx_samples;
    bool reception_complete = false;
    size_t retry_rx = 0;
    size_t num_acc_samps = 0;
    bool callback_success = false;
    size_t num_curr_rx_samps;
    std::vector<std::complex<float>> buff(max_rx_packet_size);
    int retry_count = 0;

    while (not reception_complete and not stop_signal_called)
    {
        size_t size_rx = (req_num_rx_samps == 0) ? max_rx_packet_size : std::min(req_num_rx_samps - num_acc_samps, max_rx_packet_size);

        uhd::rx_metadata_t md;
        num_curr_rx_samps = rx_streamer->recv(&buff.front(), size_rx, md, timeout, false);
        timeout = burst_pkt_time; // small timeout for subsequent packets

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
        {
            LOG_WARN("Timeout while streaming");
            success = false;
        }
        else if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW)
        {
            LOG_WARN("*** Got an overflow indication.");
        }
        else if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_LATE_COMMAND)
        {
            LOG_WARN_FMT("ERROR : %1% .. A stream command was issued in the past and expired presently.", md.strerror());
            stream_cmd.stream_now = true; // start receiving immediately
            rx_streamer->issue_stream_cmd(stream_cmd);
            success = false;
        }
        else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
        {
            LOG_WARN_FMT("Receiver error: %1%", md.strerror());
            success = false;
        }

        // TODO: catch reception error gracefully without breaking
        if (not success)
        {
            LOG_WARN("*** Reception of stream data UNSUCCESSFUL! ***");
            if (retry_count > 3)
                break;
            else
            {
                retry_count++;
                success = true;
                continue;
            }
        }
        else
            retry_count = 0;

        // run callback
        callback_success = callback(buff, num_curr_rx_samps, md.time_spec);

        // process (save, update counters, etc...) received samples and continue
        if (is_save_to_file and req_num_rx_samps == 0) // continuous saving
        {
            std::vector<std::complex<float>> forward(buff.begin(), buff.begin() + num_curr_rx_samps);
            save_stream_to_file(filename, rx_save_stream, forward);
        }
        else if (req_num_rx_samps > 0 || duration > 0) // fixed number of samples -- save in a separate vector to return
            rx_samples.insert(rx_samples.end(), buff.begin(), buff.begin() + num_curr_rx_samps);

        if (callback_success)
            reception_complete = true;
        else if (req_num_rx_samps == 0 and duration > 0) // check if rx duration expired
        {
            if ((usrp->get_time_now() - usrp_now).get_real_secs() > duration)
                reception_complete = true;
        }
        else if (req_num_rx_samps > 0) // check if all rx samples received
        {
            num_acc_samps += num_curr_rx_samps;
            if (num_acc_samps >= req_num_rx_samps)
                reception_complete = true;
        }
    }

    if (stream_cmd.stream_mode == uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS)
    {
        stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
        rx_streamer->issue_stream_cmd(stream_cmd);
    }

    if (req_num_rx_samps > 0 and num_acc_samps < req_num_rx_samps)
        LOG_WARN("Not all packets received!");

    if ((req_num_rx_samps > 0 or duration > 0) and is_save_to_file) // save all received samples if a total number of desired rx samples given
        save_stream_to_file(filename, rx_save_stream, rx_samples);

    if (rx_save_stream.is_open())
        rx_save_stream.close();

    if (success and (req_num_rx_samps > 0 or duration > 0))
        return rx_samples;
    else // do not return anything if total num rx samps not given
        return std::vector<std::complex<float>>{};
};

void USRP_class::receive_save_with_timer(bool &stop_signal_called, const float &duration)
{
    std::string data_filename, timer_filename;
    std::string homeDirStr = get_home_dir();
    std::string curr_datetime = currentDateTimeFilename();
    data_filename = homeDirStr + "/OTA-C/ProjectRoot/storage/rx_saved_file_data_" + parser.getValue_str("device-id") + "_" + curr_datetime + ".dat";
    timer_filename = homeDirStr + "/OTA-C/ProjectRoot/storage/rx_saved_file_timer_" + parser.getValue_str("device-id") + "_" + curr_datetime + ".dat";

    std::ofstream rx_save_datastream, rx_save_timer;
    rx_save_timer.open(timer_filename, std::ios::out | std::ios::binary | std::ios::app);

    bool success = true;

    // setup streaming
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    stream_cmd.num_samps = max_rx_packet_size;

    auto usrp_now = usrp->get_time_now();
    stream_cmd.stream_now = true;
    rx_streamer->issue_stream_cmd(stream_cmd);

    const double burst_pkt_time = std::max<double>(0.1, (2.0 * max_rx_packet_size / rx_rate));
    double timeout = burst_pkt_time;

    std::vector<std::complex<float>> rx_samples;
    bool reception_complete = false;
    size_t retry_rx = 0;
    size_t num_acc_samps = 0;
    bool callback_success = false;
    std::vector<std::complex<float>> buff(max_rx_packet_size);

    std::vector<double> timer_seq;

    while (not reception_complete and not stop_signal_called)
    {
        size_t size_rx = max_rx_packet_size;

        uhd::rx_metadata_t md;
        size_t num_curr_rx_samps = rx_streamer->recv(&buff.front(), size_rx, md, timeout, false);

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
        {
            LOG_WARN_FMT("Timeout while streaming");
            success = false;
        }
        else if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW)
        {
            LOG_WARN("*** Got an overflow indication.");
        }
        else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
        {
            LOG_WARN_FMT("Receiver error: %1%", md.strerror());
            success = false;
        }

        // TODO: catch reception error gracefully without breaking
        if (not success)
            break;

        std::vector<std::complex<float>> forward(buff.begin(), buff.begin() + num_curr_rx_samps);
        save_stream_to_file(data_filename, rx_save_datastream, forward);

        double time_data = md.time_spec.get_real_secs();
        LOG_INFO_FMT("Rx data at time : %1%", time_data);
        rx_save_timer.write(reinterpret_cast<char *>(&time_data), sizeof(time_data));
        rx_save_timer.write(reinterpret_cast<char *>(&num_curr_rx_samps), sizeof(num_curr_rx_samps));

        if ((usrp->get_time_now() - usrp_now).get_real_secs() > duration)
            reception_complete = true;
    }

    rx_save_timer.close();

    if (stream_cmd.stream_mode == uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS)
    {
        stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
        rx_streamer->issue_stream_cmd(stream_cmd);
    }
};

void USRP_class::adjust_for_freq_offset(const float &freq_offset)
{
    // set the sample rate
    int channel = 0; // we only use one channel on each device
    float new_rx_rate = rx_rate - freq_offset;
    float new_tx_rate = tx_rate - freq_offset;
    LOG_DEBUG_FMT("Re-Setting Tx/Rx Rate: %1% Msps.", (new_rx_rate / 1e6));
    int closest_pow_2 = std::floor(std::log2(56e6 / new_rx_rate));
    int master_clock_mul = std::pow(2, closest_pow_2);
    usrp->set_master_clock_rate(new_rx_rate * master_clock_mul);
    usrp->get_tree()->access<double>("/mboards/0/tick_rate").set(new_rx_rate * master_clock_mul);
    usrp->set_rx_rate(new_rx_rate, channel);
    usrp->set_tx_rate(new_tx_rate);
    LOG_DEBUG_FMT("New Rx rate after changing Master Clock Rate is %1%", usrp->get_rx_rate());
    tx_rate = usrp->get_tx_rate(channel);
    rx_rate = usrp->get_rx_rate(channel);
};