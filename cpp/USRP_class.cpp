#include "USRP_class.hpp"

USRP_class::USRP_class(const ConfigParser &parser) : parser(parser){};

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

    std::cout << boost::format("Waiting for \"%s\": ") % sensor_name;
    std::cout.flush();

    while (true)
    {
        if (lock_detected and (std::chrono::steady_clock::now() > setup_timeout))
        {
            std::cout << " locked." << std::endl;
            break;
        }
        if (get_sensor_fn_rx(sensor_name, 0).to_bool())
        {
            std::cout << "+";
            std::cout.flush();
            lock_detected = true;
        }
        else
        {
            if (std::chrono::steady_clock::now() > setup_timeout)
            {
                std::cout << std::endl;
                throw std::runtime_error(
                    str(boost::format(
                            "timed out waiting for consecutive locks on sensor \"%s\"") %
                        sensor_name));
            }
            std::cout << "_";
            std::cout.flush();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << std::endl;
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

    std::cout << boost::format("Waiting for \"%s\": ") % sensor_name;
    std::cout.flush();

    while (true)
    {
        if (lock_detected and (std::chrono::steady_clock::now() > setup_timeout))
        {
            std::cout << " locked." << std::endl;
            break;
        }
        if (get_sensor_fn_tx(sensor_name, 0).to_bool())
        {
            std::cout << "+";
            std::cout.flush();
            lock_detected = true;
        }
        else
        {
            if (std::chrono::steady_clock::now() > setup_timeout)
            {
                std::cout << std::endl;
                throw std::runtime_error(
                    str(boost::format(
                            "timed out waiting for consecutive locks on sensor \"%s\"") %
                        sensor_name));
            }
            std::cout << "_";
            std::cout.flush();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << std::endl;
    return true;
}

void USRP_class::initialize()
{
    // Entire routine to setup USRP, streamers, testing Rx/Tx capabilities, etc.
    std::string device_id = parser.getValue_str("device-id");
    std::string node_type = parser.getValue_str("node-type");

    bool device_create = false;
    std::string args = "serial=" + device_id;

    for (int i = 0; i < 3; ++i)
    {
        try
        {
            usrp = uhd::usrp::multi_usrp::make(args);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            device_create = true;
            break;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
        }
    }

    if (not device_create)
        throw std::runtime_error("ERROR: Failed to create device.. Exiting!");

    // set current time to zero
    usrp->set_time_now(uhd::time_spec_t(0.0));

    std::cout << boost::format("Initilizing Device: %s") % usrp->get_pp_string() << std::endl;

    //_____________________ SETUP STREAMERS _____________________
    usrp->set_clock_source("internal");

    // set the sample rate
    float rate = parser.getValue_float("rate");
    int channel = 0;
    if (rate <= 0.0)
        throw std::invalid_argument("Specify a valid sampling rate!");
    std::cout << boost::format("Setting Tx/Rx Rate: %f Msps...") % (rate / 1e6) << std::endl;
    usrp->set_tx_rate(rate);
    usrp->set_rx_rate(rate, channel);
    std::cout << boost::format("Actual Tx Rate: %f Msps...") % (usrp->get_tx_rate(channel) / 1e6) << std::endl;
    std::cout << boost::format("Actual Rx Rate: %f Msps...") % (usrp->get_rx_rate(channel) / 1e6) << std::endl;
    tx_rate = usrp->get_tx_rate(channel);
    rx_rate = usrp->get_rx_rate(channel);

    // set the center frequency
    float freq = parser.getValue_float("freq");
    float lo_offset = parser.getValue_float("lo-offset");
    std::cout << boost::format("Setting TX/RX Freq: %f MHz...") % (freq / 1e6) << std::endl;
    std::cout << boost::format("Setting TX/RX LO Offset: %f MHz...") % (lo_offset / 1e6) << std::endl;
    uhd::tune_request_t tune_request(freq, lo_offset);
    usrp->set_rx_freq(tune_request, channel);
    usrp->set_tx_freq(tune_request, channel);
    std::cout << boost::format("Actual Rx Freq: %f MHz...") % (usrp->get_rx_freq(channel) / 1e6) << std::endl;
    std::cout << boost::format("Actual Tx Freq: %f MHz...") % (usrp->get_tx_freq(channel) / 1e6) << std::endl;
    carrier_freq = usrp->get_rx_freq(channel);

    // set tx/rx gains
    float _rx_gain, _tx_gain;
    if (node_type == "leaf")
    {
        _rx_gain = parser.getValue_float("rx-gain");
        _tx_gain = parser.getValue_float("tx-gain");
    }
    else
    {
        _rx_gain = parser.getValue_float("cent-rx-gain");
        _tx_gain = parser.getValue_float("cent-tx-gain");
    }
    if (_rx_gain >= 0.0)
    {
        std::cout << boost::format("Setting RX Gain: %f dB...") % _rx_gain << std::endl;
        usrp->set_rx_gain(_rx_gain, channel);
        rx_gain = usrp->get_rx_gain(channel);
        std::cout << boost::format("Actual Rx Gain: %f dB...") % rx_gain << std::endl;
    }
    if (_tx_gain >= 0.0)
    {
        std::cout << boost::format("Setting TX Gain: %f dB...") % _tx_gain << std::endl;
        usrp->set_tx_gain(_tx_gain, channel);
        tx_gain = usrp->get_tx_gain(channel);
        std::cout << boost::format("Actual Tx Gain: %f dB...") % tx_gain << std::endl;
    }
    // set the IF filter bandwidth
    float _rx_bw = parser.getValue_float("rx-bw");
    if (_rx_bw >= 0.0)
    {
        std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % (_rx_bw / 1e6) << std::endl;
        usrp->set_rx_bandwidth(_rx_bw, channel);
        rx_bw = usrp->get_rx_bandwidth(channel);
        std::cout << boost::format("Actual Rx Bandwidth: %f MHz...") % (rx_bw / 1e6) << std::endl;
    }
    float _tx_bw = parser.getValue_float("tx-bw");
    if (_tx_bw >= 0.0)
    {
        std::cout << boost::format("Setting TX Bandwidth: %f MHz...") % (_tx_bw / 1e6) << std::endl;
        usrp->set_tx_bandwidth(_tx_bw, channel);
        tx_bw = usrp->get_tx_bandwidth(channel);
        std::cout << boost::format("Actual Tx Bandwidth: %f MHz...") % (tx_bw / 1e6) << std::endl;
    }
    // sleep a bit to allow setup
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    // create streamers
    uhd::stream_args_t stream_args("fc32", "sc16");
    std::vector<size_t> channel_nums;
    channel_nums.push_back(channel);
    stream_args.channels = channel_nums;
    rx_streamer = usrp->get_rx_stream(stream_args);
    tx_streamer = usrp->get_tx_stream(stream_args);

    // check Ref and LO Lock detect
    check_locked_sensor_rx();
    check_locked_sensor_tx();

    // set max packet size values
    max_rx_packet_size = rx_streamer->get_max_num_samps();
    max_tx_packet_size = tx_streamer->get_max_num_samps();

    // set sampling durations
    rx_sample_duration = 1 / rx_rate;
    tx_sample_duration = 1 / tx_rate;

    //_____________________ TEST TX - RX _____________________
    std::vector<std::complex<float>> tx_buff(max_tx_packet_size, std::complex<float>(0.1, 0.1));
    bool tx_success = transmission(tx_buff, uhd::time_spec_t(0.0));
    if (tx_success)
        std::cout << "Transmit test successful!" << std::endl;
    else
    {
        std::cerr << "Transmit test failed!" << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    size_t num_pkts = 10;
    auto rx_samples = reception(max_rx_packet_size * num_pkts, uhd::time_spec_t(0.0));
    if (rx_samples.size() == max_rx_packet_size * num_pkts)
        std::cout << "Reception test successful! Total " << rx_samples.size() << " samples received." << std::endl;
    else
    {
        std::cerr << "Reception test Failed!" << std::endl;
    }

    // compute backgroud noise
    auto zfc_seq = generateZadoffChuSequence(parser.getValue_int("Ref-N-zfc"), parser.getValue_int("Ref-m-zfc"), 1.0);
    for (int i = 0; i < rx_samples.size() - zfc_seq.size(); ++i)
    {
        auto corr = std::complex<float>(0.0, 0.0);
        for (int j = 0; j < zfc_seq.size(); ++j)
        {
            corr += rx_samples[i + j] * std::conj(zfc_seq[j]);
        }
        init_background_noise += std::abs(corr) / zfc_seq.size();
    }

    init_background_noise = init_background_noise / (rx_samples.size() - zfc_seq.size());
    std::cout << "Average background noise for packets = " << init_background_noise << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "--------- USRP initilization finished -----------------" << std::endl
              << std::endl;
};

bool USRP_class::transmission(const std::vector<std::complex<float>> &buff, const uhd::time_spec_t &tx_time, bool ask_ack)
{
    bool success = false;
    size_t total_num_samps = buff.size();

    // setup metadata for the first packet
    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst = false;

    auto usrp_now = usrp->get_time_now();
    if (tx_time <= usrp_now or tx_time == uhd::time_spec_t(0.0))
    {
        std::cerr << "tx_time < current USRP time (time-diff : " << (tx_time - usrp_now).get_real_secs() * 1e6 << " microsecs). Transmitting without delay." << std::endl;
        md.has_time_spec = false;
    }
    else
    {
        md.has_time_spec = true;
        md.time_spec = tx_time;
    }

    const double burst_pkt_time = std::max<double>(0.1, (2.0 * max_tx_packet_size / tx_rate));
    double tx_delay = md.has_time_spec ? (tx_time - usrp_now).get_real_secs() : 0.0;
    double timeout = burst_pkt_time + tx_delay;

    // transmission
    size_t num_acc_samps = 0;
    if (DEBUG)
        std::cout << "Starting transmission of " << total_num_samps << " samples." << std::endl;

    while (num_acc_samps < total_num_samps and not stop_signal_called)
    {
        size_t samps_to_send = std::min(total_num_samps - num_acc_samps, max_tx_packet_size);
        const size_t num_tx_samps_sent_now = tx_streamer->send(&buff.front(), samps_to_send, md, timeout);
        if (num_tx_samps_sent_now < samps_to_send)
            std::cerr << "TX-TIMEOUT: Transmission timeout!!" << std::endl;

        md.start_of_burst = false;
        md.has_time_spec = false;
        num_acc_samps += num_tx_samps_sent_now;
    }

    // send a mini EOB packet
    md.end_of_burst = true;
    tx_streamer->send("", 0, md);

    if (ask_ack)
    { // check for ASYNC message
        if (DEBUG)
            std::cout << std::endl
                      << "Waiting for async burst ACK... " << std::flush;
        uhd::async_metadata_t async_md;
        bool got_async_burst_ack = false;
        // loop through all messages for the ACK packet (may have underflow messages in queue)
        while (not got_async_burst_ack and tx_streamer->recv_async_msg(async_md, timeout))
            got_async_burst_ack = (async_md.event_code == uhd::async_metadata_t::EVENT_CODE_BURST_ACK);
        if (DEBUG)
            std::cout << (got_async_burst_ack ? "success" : "fail") << std::endl
                      << std::endl;

        if (not got_async_burst_ack)
            std::cerr << "ACK FAIL..! " << std::endl;
        else
            success = true;
    }
    else
    {
        if (num_acc_samps >= total_num_samps)
            success = true;
    }

    return success;
};

std::vector<std::complex<float>> USRP_class::reception(const size_t &num_rx_samps, const uhd::time_spec_t &rx_time)
{
    bool success = false;
    uhd::rx_metadata_t md;

    // setup streaming
    uhd::stream_cmd_t stream_cmd(num_rx_samps > max_rx_packet_size ? uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS : uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);

    stream_cmd.num_samps = num_rx_samps;

    auto usrp_now = usrp->get_time_now();

    if (rx_time <= usrp_now or rx_time == uhd::time_spec_t(0.0))
    {
        std::cerr << "rx_time < current USRP time (time-diff : " << (rx_time - usrp_now).get_real_secs() * 1e6 << " microsecs). Receiving without delay." << std::endl;
        stream_cmd.stream_now = true;
    }
    else
    {
        stream_cmd.stream_now = false;
        stream_cmd.time_spec = rx_time;
    }

    rx_streamer->issue_stream_cmd(stream_cmd);

    const double burst_pkt_time = std::max<double>(0.1, (2.0 * max_rx_packet_size / rx_rate));
    double rx_delay = stream_cmd.stream_now ? 0.0 : (rx_time - usrp_now).get_real_secs();
    double timeout = burst_pkt_time + rx_delay;

    std::vector<std::complex<float>> buff(num_rx_samps, std::complex<float>(0.0, 0.0));

    size_t num_acc_samps = 0;

    while (num_acc_samps < num_rx_samps and not stop_signal_called)
    {
        size_t size_rx = std::min(num_rx_samps - num_acc_samps, max_rx_packet_size);

        try
        {
            num_acc_samps += rx_streamer->recv(&buff.front(), size_rx, md, timeout, false);
        }
        catch (uhd::io_error e)
        {
            std::string error_msg = "Caught an IO exception in CSD Receiver Thread. ERROR : " + static_cast<std::string>(e.what());
            std::cerr << error_msg << std::endl;
        }

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
        {
            std::cout << boost::format("Timeout while streaming") << std::endl;
        }
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW)
        {
            std::cerr << "*** Got an overflow indication." << std::endl;
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
        {
            std::string error = str(boost::format("Receiver error: %s") % md.strerror());
            std::cerr << error << std::endl;
        }
    }

    stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
    rx_streamer->issue_stream_cmd(stream_cmd);

    if (num_acc_samps < num_rx_samps)
    {
        std::cerr << "Not all packets received!" << std::endl;
    }
    else
    {
        success = true;
    }

    if (success)
        return buff;
    else
        return std::vector<std::complex<float>>{};
};

void USRP_class::gain_adjustment(const float &est_e2e_sig_amp, const float &min_path_loss_dB)
{
    /* Adjustment of leaf Rx gain for better reception */
    // calculate Path loss (dB) from est_ch_pow
    float cent_tx_gain = parser.getValue_float("cent-tx-gain"); // dB
    float leaf_rx_gain = rx_gain;                               // dB
    float e2e_gain = amplitudeToDb(est_e2e_sig_amp);
    float path_loss_pow = e2e_gain - cent_tx_gain - leaf_rx_gain;

    // adjust tx-rx gains to enable better scaling of signals
    float min_ch_amp = parser.getValue_float("min-ch-pow");
    float min_ch_pow_dB = amplitudeToDb(min_ch_amp);
    float min_est_diff = min_ch_pow_dB - path_loss_pow;

    // first, estimated e2e_gain should not be

    // min_est_diff should be
    float diff = cent_tx_gain + leaf_rx_gain;
}