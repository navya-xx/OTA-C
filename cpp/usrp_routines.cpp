#include "usrp_routines.hpp"

bool check_locked_sensor(std::vector<std::string> sensor_names,
                         const char *sensor_name,
                         get_sensor_fn_t get_sensor_fn,
                         double setup_time)
{
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
        if (get_sensor_fn(sensor_name).to_bool())
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

// routines to setup USRP
// create a usrp device
std::pair<uhd::rx_streamer::sptr, uhd::tx_streamer::sptr> create_usrp_streamers(uhd::usrp::multi_usrp::sptr &usrp, ConfigParser &config_parser)
{
    // Lock mboard clocks
    std::string ref = config_parser.getValue_str("ref");
    if (ref != "")
    {
        std::cout << "Setting clock source as '" << ref << "'." << std::endl;
        usrp->set_clock_source(ref);
    }

    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    // set the sample rate
    float rate = config_parser.getValue_float("rate");
    int channel = 0;
    if (rate <= 0.0)
    {
        throw std::runtime_error("Please specify a valid sample rate");
    }

    std::cout << boost::format("Setting Rate: %f Msps...") % (rate / 1e6) << std::endl;
    usrp->set_rx_rate(rate, channel);
    usrp->set_tx_rate(rate);

    std::cout << boost::format("Actual Rx Rate: %f Msps...") % (usrp->get_rx_rate(channel) / 1e6) << std::endl;
    std::cout << boost::format("Actual Tx Rate: %f Msps...") % (usrp->get_tx_rate(channel) / 1e6) << std::endl;

    // set the center frequency
    float freq = config_parser.getValue_float("freq");
    float lo_offset = config_parser.getValue_float("lo-offset");

    std::cout << boost::format("Setting Freq: %f MHz...") % (freq / 1e6) << std::endl;
    std::cout << boost::format("Setting LO Offset: %f MHz...") % (lo_offset / 1e6) << std::endl;
    uhd::tune_request_t tune_request(freq, lo_offset);
    usrp->set_rx_freq(tune_request, channel);
    usrp->set_tx_freq(tune_request, channel);
    std::cout << boost::format("Actual Rx Freq: %f MHz...") % (usrp->get_rx_freq(channel) / 1e6) << std::endl;
    std::cout << boost::format("Actual Tx Freq: %f MHz...") % (usrp->get_tx_freq(channel) / 1e6) << std::endl;

    // set the rf gain
    float rx_gain = config_parser.getValue_float("rx-gain");
    if (rx_gain >= 0.0)
    {
        std::cout << boost::format("Setting RX Gain: %f dB...") % rx_gain << std::endl;
        usrp->set_rx_gain(rx_gain, channel);
        std::cout << boost::format("Actual Rx Gain: %f dB...") % usrp->get_rx_gain(channel) << std::endl;
    }
    float tx_gain = config_parser.getValue_float("tx-gain");
    if (tx_gain >= 0.0)
    {
        std::cout << boost::format("Setting TX Gain: %f dB...") % tx_gain << std::endl;
        usrp->set_tx_gain(tx_gain, channel);
        std::cout << boost::format("Actual Tx Gain: %f dB...") % usrp->get_tx_gain(channel) << std::endl;
    }

    // set the IF filter bandwidth
    float rx_bw = config_parser.getValue_float("rx-bw");
    if (rx_bw >= 0.0)
    {
        std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % (rx_bw / 1e6) << std::endl;
        usrp->set_rx_bandwidth(rx_bw, channel);
        std::cout << boost::format("Actual Rx Bandwidth: %f MHz...") % (usrp->get_rx_bandwidth(channel) / 1e6) << std::endl;
    }
    float tx_bw = config_parser.getValue_float("tx-bw");
    if (tx_bw >= 0.0)
    {
        std::cout << boost::format("Setting TX Bandwidth: %f MHz...") % (tx_bw / 1e6) << std::endl;
        usrp->set_tx_bandwidth(tx_bw, channel);
        std::cout << boost::format("Actual Tx Bandwidth: %f MHz...") % (usrp->get_tx_bandwidth(channel) / 1e6) << std::endl;
    }

    // sleep a bit to allow setup
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // check Ref and LO Lock detect
    check_locked_sensor(
        usrp->get_rx_sensor_names(channel),
        "lo_locked",
        [usrp, channel](const std::string &sensor_name)
        {
            return usrp->get_rx_sensor(sensor_name, channel);
        },
        1.0);

    check_locked_sensor(
        usrp->get_tx_sensor_names(channel),
        "lo_locked",
        [usrp, channel](const std::string &sensor_name)
        {
            return usrp->get_tx_sensor(sensor_name, channel);
        },
        1.0);

    // create streamers

    uhd::stream_args_t stream_args("fc32", "sc16");
    std::vector<size_t> channel_nums;
    channel_nums.push_back(channel);
    stream_args.channels = channel_nums;
    uhd::rx_streamer::sptr rx_streamer = usrp->get_rx_stream(stream_args);
    uhd::tx_streamer::sptr tx_streamer = usrp->get_tx_stream(stream_args);

    return {rx_streamer, tx_streamer};
}

float get_background_noise_level(uhd::usrp::multi_usrp::sptr &usrp, uhd::rx_streamer::sptr &rx_streamer)
{
    uhd::rx_metadata_t noise_md;
    uhd::stream_cmd_t noise_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
    size_t spb = rx_streamer->get_max_num_samps();
    noise_stream_cmd.num_samps = size_t(spb);
    noise_stream_cmd.stream_now = true;
    noise_stream_cmd.time_spec = uhd::time_spec_t();
    rx_streamer->issue_stream_cmd(noise_stream_cmd);

    size_t noise_seq_len = spb * 1;

    std::vector<std::complex<float>> noise_buff(noise_seq_len);

    try
    {
        rx_streamer->recv(&noise_buff.front(), noise_seq_len, noise_md, 1.0, false);
    }
    catch (uhd::io_error &e)
    {
        std::cerr << "Caught an IO exception. " << std::endl;
        std::cerr << e.what() << std::endl;
    }

    float noise_level = 0.0;
    for (int i = 0; i < noise_seq_len; i++)
    {
        noise_level += std::abs(noise_buff[i]);
    }

    return noise_level / noise_seq_len;
}

void cyclestartdetector_receiver_thread(CycleStartDetector &csdbuffer, uhd::rx_streamer::sptr rx_stream, std::atomic<bool> &stop_thread_signal, bool &stop_signal_called, const std::chrono::time_point<std::chrono::steady_clock> &stop_time, const float &rate)
{
    uhd::rx_metadata_t md;
    bool overflow_message = true;
    size_t spb = rx_stream->get_max_num_samps();

    // setup streaming
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    stream_cmd.num_samps = size_t(spb);
    stream_cmd.stream_now = true;
    // stream_cmd.time_spec = uhd::time_spec_t();
    rx_stream->issue_stream_cmd(stream_cmd);

    std::vector<std::complex<float>> buff(spb);

    const float burst_pkt_time = std::max<float>(0.100f, (2 * spb / rate));
    float recv_timeout = burst_pkt_time + 0.05;
    size_t num_rx_samps = 0;

    // Run this loop until either time expired (if a duration was given), until
    // the requested number of samples were collected (if such a number was
    // given), or until Ctrl-C was pressed.

    while (not stop_signal_called and not stop_thread_signal and not(std::chrono::steady_clock::now() > stop_time))
    {
        try
        {
            num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, recv_timeout, false);
            // recv_timeout = burst_pkt_time;
        }
        catch (uhd::io_error &e)
        {
            std::cerr << "Caught an IO exception in CSD Receiver Thread. " << std::endl;
            std::cerr << e.what() << std::endl;
            return;
        }

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
        {
            std::cout << boost::format("Timeout while streaming") << std::endl;
            break;
        }
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW)
        {
            if (overflow_message)
            {
                overflow_message = false;
                std::cerr << "*** Got an overflow indication." << std::endl
                          << std::endl;
                ;
            }
            continue;
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
        {
            std::string error = str(boost::format("Receiver error: %s") % md.strerror());
            std::cerr << error << std::endl;
        }

        // pass on to the cycle start detector
        csdbuffer.produce(buff, num_rx_samps, md.time_spec);

    } // while loop end
    const auto actual_stop_time = std::chrono::steady_clock::now();

    // issue stop streaming command
    stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
    rx_stream->issue_stream_cmd(stream_cmd);

    // if (DEBUG)
    //     std::cout << "Ending receiver thread..." << std::endl;
}

void cyclestartdetector_transmitter_thread(CycleStartDetector &csdbuffer, uhd::rx_streamer::sptr rx_stream, std::atomic<bool> &stop_thread_signal, bool &stop_signal_called, const std::chrono::time_point<std::chrono::steady_clock> &stop_time, const float &rate)
{

    bool result;

    while (not stop_signal_called and not stop_thread_signal and not(std::chrono::steady_clock::now() > stop_time))
    {
        result = csdbuffer.consume();

        // check result
        if (result)
        {
            if (DEBUG)
                std::cout << "Finished detection process! Closing thread..." << std::endl;

            stop_thread_signal = true;
            break;
        }
    }
}

void csdtest_tx_leaf_node(uhd::usrp::multi_usrp::sptr &usrp, uhd::tx_streamer::sptr &tx_stream, const size_t &N_zfc, const size_t &m_zfc, const float &csd_ch_pow, const uhd::time_spec_t &csd_detect_time, const float &min_ch_pow, const float &tx_wait)
{

    auto tx_zfc_seq = generateZadoffChuSequence(N_zfc, m_zfc);

    if (DEBUG)
    {
        std::cout << "TX ZFC seq len " << N_zfc << ", identifier " << m_zfc << std::endl;
        std::cout << "Channel power estimated : " << csd_ch_pow << ", min ch pow " << min_ch_pow << std::endl;
    }

    // size_t num_max_tx_samps = tx_stream->get_max_num_samps();
    std::vector<std::complex<float>> tx_buff(N_zfc);

    for (size_t n = 0; n < N_zfc; n++)
    {
        // tx_buff[n] = tx_zfc_seq[n % N_zfc] / csd_ch_pow * min_ch_pow;
        tx_buff[n] = tx_zfc_seq[n % N_zfc];
    }

    const uhd::time_spec_t tx_timer = csd_detect_time + uhd::time_spec_t(tx_wait / 1e6);
    uhd::tx_metadata_t txmd;
    txmd.has_time_spec = true;
    txmd.start_of_burst = true;
    txmd.end_of_burst = false;

    const double timeout = (tx_timer - usrp->get_time_now()).get_real_secs() + 0.1;

    txmd.time_spec = tx_timer;

    if (DEBUG)
    {
        std::cout << "Current USRP timer : " << static_cast<int64_t>(usrp->get_time_now().get_real_secs() * 1e6) << ", desired timer : " << static_cast<int64_t>(txmd.time_spec.get_real_secs() * 1e6) << std::flush;
        std::cout << ", Difference " << static_cast<int64_t>((usrp->get_time_now() - txmd.time_spec).get_real_secs() * 1e6) << std::endl;
    }
    size_t num_tx_samps = tx_stream->send(&tx_buff.front(), N_zfc, txmd, timeout);

    // send a mini EOB packet
    txmd.has_time_spec = false;
    txmd.start_of_burst = false;
    txmd.end_of_burst = true;
    tx_stream->send("", 0, txmd);

    if (DEBUG)
        std::cout << std::endl
                  << "Waiting for async burst ACK... " << std::flush;
    uhd::async_metadata_t async_md;
    bool got_async_burst_ack = false;
    // loop through all messages for the ACK packet (may have underflow messages in queue)
    while (not got_async_burst_ack and tx_stream->recv_async_msg(async_md, timeout))
    {
        got_async_burst_ack =
            (async_md.event_code == uhd::async_metadata_t::EVENT_CODE_BURST_ACK);
    }
    if (DEBUG)
        std::cout << (got_async_burst_ack ? "success" : "fail") << std::endl;

    if (DEBUG)
        std::cout << "Transmitted " << num_tx_samps << " samples. Current USRP timer : " << static_cast<int64_t>(usrp->get_time_now().get_real_secs() * 1e6) << ", desired timer : " << static_cast<int64_t>(txmd.time_spec.get_real_secs() * 1e6) << std::endl;
}

uhd::time_spec_t csd_tx_ref_signal(uhd::usrp::multi_usrp::sptr &usrp, uhd::tx_streamer::sptr &tx_stream, const size_t &Ref_N_zfc, const size_t &Ref_m_zfc, const size_t &Ref_R_zfc, const uhd::time_spec_t &tx_wait_time, bool &stop_signal_called)
{
    // pre-fill the buffer with the waveform
    auto zfc_seq = generateZadoffChuSequence(Ref_N_zfc, Ref_m_zfc);

    std::cout << "ZFC seq len " << Ref_N_zfc << ", identifier " << Ref_m_zfc << " Reps " << Ref_R_zfc << std::endl;

    // allocate a buffer which we re-use for each channel
    size_t spb = Ref_N_zfc * Ref_R_zfc;
    std::vector<std::complex<float>> buff(spb);

    for (size_t n = 0; n < spb; n++)
    {
        buff[n] = zfc_seq[n % Ref_N_zfc];
    }

    // Set up metadata. We start streaming a bit in the future
    // to allow MIMO operation:
    uhd::tx_metadata_t txmd;
    txmd.start_of_burst = true;
    txmd.end_of_burst = false;
    txmd.has_time_spec = true;
    txmd.time_spec = usrp->get_time_now() + tx_wait_time;

    // send the entire contents of the buffer
    size_t num_acc_samps = tx_stream->send(&buff.front(), spb, txmd);

    // send a mini EOB packet
    txmd.has_time_spec = false;
    txmd.start_of_burst = false;
    txmd.end_of_burst = true;
    tx_stream->send("", 0, txmd);

    // finished
    if (DEBUG)
        std::cout << "Total number of samples transmitted: " << num_acc_samps << std::endl;

    return txmd.time_spec;
}

void csd_rx_test_signal(uhd::usrp::multi_usrp::sptr &usrp, uhd::rx_streamer::sptr &rx_stream, const size_t &test_signal_len, const uhd::time_spec_t &rx_time, const size_t &total_rx_samples, const std::string &file, bool &stop_signal_called)
{
    uhd::rx_metadata_t rxmd;
    uhd::stream_cmd_t rx_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    rx_stream_cmd.stream_now = false;
    rx_stream_cmd.time_spec = rx_time;
    size_t num_total_samps = 0;
    rx_stream_cmd.num_samps = total_rx_samples;
    rx_stream->issue_stream_cmd(rx_stream_cmd);

    std::vector<std::complex<float>> rx_buff(total_rx_samples);
    float sample_duration = 1 / usrp->get_rx_rate();
    uhd::time_spec_t usrp_time_now = usrp->get_time_now();
    double timeout = (rx_time - usrp_time_now).get_real_secs() + total_rx_samples * sample_duration + 0.1;

    if (DEBUG)
        std::cout << "Time diff : " << (rx_time - usrp_time_now).get_real_secs() * 1e6 << " = " << rx_time.get_real_secs() * 1e6 << " - " << usrp_time_now.get_real_secs() * 1e6 << std::endl;

    std::ofstream outfile;
    outfile.open(file.c_str(), std::ofstream::binary);

    bool overflow_message = true;
    bool continue_on_bad_packet = true;

    while (not stop_signal_called and (num_total_samps < total_rx_samples))
    {

        size_t num_rx_samps = rx_stream->recv(&rx_buff.front(), rx_buff.size(), rxmd, timeout, false);

        if (rxmd.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
        {
            std::cout << boost::format("Timeout while streaming") << std::endl;
            break;
        }
        if (rxmd.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW)
        {
            if (overflow_message)
            {
                overflow_message = false;
                std::cerr
                    << boost::format(
                           "Got an overflow indication. Please consider the following:\n"
                           "  Your write medium must sustain a rate of %fMB/s.\n"
                           "  Dropped samples will not be written to the file.\n"
                           "  Please modify this example for your purposes.\n"
                           "  This message will not appear again.\n") %
                           (usrp->get_rx_rate() * sizeof(std::complex<float>) / 1e6);
            }
            continue;
        }
        if (rxmd.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
        {
            std::string error = str(boost::format("Receiver error: %s") % rxmd.strerror());
            if (continue_on_bad_packet)
            {
                std::cerr << error << std::endl;
                continue;
            }
            else
                throw std::runtime_error(error);
        }

        num_total_samps += num_rx_samps;

        if (outfile.is_open())
        {
            outfile.write((const char *)&rx_buff.front(), num_rx_samps * sizeof(std::complex<float>));
        }
    }

    if (outfile.is_open())
    {
        outfile.close();
    }

    if (DEBUG)
        std::cout << "Total " << num_total_samps << " samples saved in file " << file << std::endl;
}