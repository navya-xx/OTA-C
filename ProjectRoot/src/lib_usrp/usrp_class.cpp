#include "usrp_class.hpp"
#include <uhd/cal/database.hpp>

USRP_class::USRP_class(const ConfigParser &parser) : USRP_init(parser), parser(parser) {};

float USRP_class::estimate_background_noise_power(const size_t &num_pkts)
{
    bool dont_stop = false;
    std::vector<sample_type> rx_samples;
    uhd::time_spec_t rx_timer;
    receive_fixed_num_samps(dont_stop, max_rx_packet_size * num_pkts, rx_samples, rx_timer);

    float noise_power = calc_signal_power(rx_samples);

    return noise_power;
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

void USRP_class::pre_process_tx_symbols(std::vector<sample_type> &tx_samples, const float &scale)
{
    if (flag_correct_cfo)
    {
        size_t cfo_counter = 0;
        correct_cfo(tx_samples, cfo_counter, scale, cfo);
    }
}

void USRP_class::post_process_rx_symbols(std::vector<sample_type> &rx_samples)
{
    if (flag_correct_cfo)
    {
        size_t cfo_counter = 0;
        correct_cfo(rx_samples, cfo_counter, 1.0, -cfo);
    }
}

bool USRP_class::transmission(const std::vector<sample_type> &buff, const uhd::time_spec_t &tx_time, bool &stop_signal_called, bool ask_ack)
{
    bool success = false;
    size_t total_num_samps = buff.size();
    // setup metadata for the first packet
    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst = false;

    auto usrp_now = get_time_now();
    double time_diff = (tx_time - usrp_now).get_real_secs();

    if (time_diff <= 0.0)
    {
        LOG_DEBUG_FMT("Transmitting %d samples WITHOUT delay.", total_num_samps);
        md.has_time_spec = false;
    }
    else
    {
        LOG_DEBUG_FMT("Transmitting %d samples WITH delay %2% microsecs.", total_num_samps, (time_diff * 1e6));
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

                    ++retry_tx_counter;
                    if (retry_tx_counter >= 5)
                    {
                        LOG_WARN_FMT("All %1% retries failed!", retry_tx_counter);
                        transmit_failure = true;
                        break;
                    }
                    else
                    {
                        LOG_WARN_FMT("Retry %1% to transmit signal again ...", retry_tx_counter);
                        time_diff = (tx_time - get_time_now()).get_real_secs();
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
                LOG_WARN_FMT("Error in transmission : %1%", e.what());
                if (retry_tx_counter >= 5)
                {
                    LOG_WARN_FMT("All %1% retries failed!", retry_tx_counter);
                    transmit_failure = true;
                    break;
                }
                else
                {
                    LOG_WARN_FMT("Retry %1% to transmit signal again ...", retry_tx_counter);
                    time_diff = (tx_time - get_time_now()).get_real_secs();
                    if (time_diff <= 0.0)
                        md.has_time_spec = false;
                    else
                    {
                        md.has_time_spec = true;
                        md.time_spec = tx_time;
                    }
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

bool USRP_class::single_burst_transmission(const std::vector<sample_type> &buff, const uhd::time_spec_t &tx_time, bool &stop_signal_called, bool ask_ack)
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

    const double burst_pkt_time = std::max<double>(0.1, (1.5 * total_num_samps / tx_rate));
    double tx_delay, timeout;

    timeout = burst_pkt_time + time_diff;
    size_t num_tx_samps_sent_now = tx_streamer->send(&buff.front(), buff.size(), md, timeout);

    if (num_tx_samps_sent_now < total_num_samps)
        return false;

    // send a mini EOB packet
    md.end_of_burst = true;
    tx_streamer->send("", 0, md);

    if (ask_ack)
    {
        LOG_INTO_BUFFER("Waiting for async burst ACK... ");

        uhd::async_metadata_t async_md;
        bool got_async_burst_ack = false;
        if (time_diff >= 0.0)
            timeout = burst_pkt_time + time_diff;
        else
            timeout = burst_pkt_time;
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
        if (num_tx_samps_sent_now == total_num_samps)
            success = true;
        else
        {
            LOG_WARN("Transmission FAILED..!");
        }
    }

    return success;
};

void USRP_class::continuous_transmission(const std::vector<sample_type> &buff, std::atomic_bool &stop_transmission)
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

std::vector<sample_type> USRP_class::reception(bool &stop_signal_called, const size_t &req_num_rx_samps, const float &duration, const uhd::time_spec_t &rx_time, bool is_save_to_file, const std::function<bool(const std::vector<sample_type> &, const size_t &, const uhd::time_spec_t &)> &callback)
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
    bool fixed_reception_condition = (req_num_rx_samps > 0 or duration > 0);

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
        if (std::floor(time_diff * 1e6) > 50000)
        {
            size_t wait_microsecs = std::floor(time_diff * 1e6) - 50000;
            LOG_DEBUG_FMT("Rx wait for %1% microsecs", wait_microsecs);
            std::this_thread::sleep_for(std::chrono::microseconds(wait_microsecs));
        }
        stream_cmd.stream_now = false;
        stream_cmd.time_spec = rx_time;
    }

    rx_streamer->issue_stream_cmd(stream_cmd);

    const double burst_pkt_time = std::max<double>(0.1, (2.0 * max_rx_packet_size / rx_rate));
    double rx_delay = stream_cmd.stream_now ? 0.0 : (rx_time - usrp->get_time_now()).get_real_secs();
    double timeout = burst_pkt_time + rx_delay;

    std::vector<sample_type> rx_samples;
    bool reception_complete = false;
    size_t retry_rx = 0;
    size_t num_acc_samps = 0;
    bool callback_success = false;
    size_t num_curr_rx_samps;
    std::vector<sample_type> buff(max_rx_packet_size);
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

        // Catch reception error gracefully without breaking
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
        if (is_save_to_file and (not fixed_reception_condition)) // continuous saving
        {
            std::vector<sample_type> forward(buff.begin(), buff.begin() + num_curr_rx_samps);
            save_stream_to_file(filename, rx_save_stream, forward);
        }

        if (fixed_reception_condition) // fixed number of samples -- save in a separate vector to return
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

    if (fixed_reception_condition and is_save_to_file) // save all received samples if a total number of desired rx samples given
        save_stream_to_file(filename, rx_save_stream, rx_samples);

    if (rx_save_stream.is_open())
        rx_save_stream.close();

    if (success and fixed_reception_condition)
        return rx_samples;
    else // do not return anything if total num rx samps not given
        return std::vector<sample_type>{};
};

void USRP_class::receive_save_with_timer(bool &stop_signal_called, const float &duration)
{
    std::string data_filename, timer_filename;
    std::string homeDirStr = get_home_dir();
    std::string curr_datetime = currentDateTimeFilename();
    data_filename = homeDirStr + "/OTA-C/ProjectRoot/storage/data_" + parser.getValue_str("device-id") + "_" + curr_datetime + ".dat";
    timer_filename = homeDirStr + "/OTA-C/ProjectRoot/storage/timer_" + parser.getValue_str("device-id") + "_" + curr_datetime + ".dat";

    bool success = true;

    // setup streaming
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    stream_cmd.num_samps = max_rx_packet_size;

    size_t total_num_samps = std::ceil(duration * rx_rate / max_rx_packet_size) * max_rx_packet_size;

    // auto start_time = std::chrono::high_resolution_clock::now();
    stream_cmd.stream_now = true;
    rx_streamer->issue_stream_cmd(stream_cmd);

    const double burst_pkt_time = std::max<double>(0.1, (2.0 * max_rx_packet_size / rx_rate));
    double timeout = burst_pkt_time;

    std::vector<sample_type> rx_samples;
    std::vector<uhd::time_spec_t> timer_vec;
    std::vector<size_t> datalen_vec;

    bool reception_complete = false;
    size_t rx_counter = 0;
    size_t num_acc_samps = 0;
    bool callback_success = false;
    std::vector<sample_type> buff(total_num_samps);

    while (num_acc_samps < total_num_samps and not stop_signal_called)
    {

        uhd::rx_metadata_t md;
        size_t num_curr_rx_samps = rx_streamer->recv(&buff.front() + num_acc_samps, max_rx_packet_size, md, timeout, false);
        num_acc_samps += num_curr_rx_samps;

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

        if (not success)
            break;

        std::cout << "\rNum of packets received so far = " << rx_counter;
        std::cout.flush();
        ++rx_counter;
        auto time_data = md.time_spec;
        timer_vec.emplace_back(time_data);
        datalen_vec.emplace_back(num_curr_rx_samps);
    }

    if (stream_cmd.stream_mode == uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS)
    {
        stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
        rx_streamer->issue_stream_cmd(stream_cmd);
    }

    std::cout << std::endl;
    std::cout << "Saving file..." << std::endl;

    std::ofstream rx_save_datastream, rx_save_timer;
    save_stream_to_file(data_filename, rx_save_datastream, buff);

    rx_save_timer.open(timer_filename, std::ios::out | std::ios::binary | std::ios::app);
    for (size_t i = 0; i < timer_vec.size(); ++i)
    {
        // LOG_INFO_FMT("Rx data at time : %1%", time_data);
        double time_data = timer_vec[i].get_real_secs();
        auto datalen = datalen_vec[i];
        rx_save_timer.write(reinterpret_cast<char *>(&time_data), sizeof(double));
        rx_save_timer.write(reinterpret_cast<char *>(&datalen), sizeof(size_t));
    }
    rx_save_timer.close();
};

void USRP_class::receive_fixed_num_samps(bool &stop_signal_called, const size_t &num_rx_samples, std::vector<sample_type> &out_samples, uhd::time_spec_t &out_timer)
{
    bool success = true;

    // setup streaming
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    stream_cmd.num_samps = max_rx_packet_size;
    out_samples.resize(num_rx_samples);

    // auto start_time = std::chrono::high_resolution_clock::now();
    stream_cmd.stream_now = true;
    rx_streamer->issue_stream_cmd(stream_cmd);

    const double burst_pkt_time = std::max<double>(0.1, (2.0 * max_rx_packet_size / rx_rate));
    double timeout = burst_pkt_time;

    size_t rx_counter = 0;
    size_t num_acc_samps = 0;

    while (num_acc_samps < num_rx_samples and not stop_signal_called)
    {
        uhd::rx_metadata_t md;
        size_t packet_size = std::min(num_rx_samples - num_acc_samps, max_rx_packet_size);
        size_t num_curr_rx_samps = rx_streamer->recv(&out_samples.front() + num_acc_samps, packet_size, md, timeout, false);
        num_acc_samps += num_curr_rx_samps;

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

        if (not success)
            break;

        if (rx_counter == 0)
            out_timer = md.time_spec;

        ++rx_counter;
    }

    stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
    rx_streamer->issue_stream_cmd(stream_cmd);
};

void USRP_class::receive_continuously_with_callback(bool &stop_signal_called, const std::function<bool(const std::vector<sample_type> &, const size_t &, const uhd::time_spec_t &)> &callback)
{
    bool success = true;

    // setup streaming
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    size_t packet_size = max_rx_packet_size;

    stream_cmd.num_samps = packet_size;
    stream_cmd.stream_now = true;
    rx_streamer->issue_stream_cmd(stream_cmd);

    const double burst_pkt_time = std::max<double>(0.1, (2.0 * packet_size / rx_rate));
    double timeout = burst_pkt_time;

    size_t rx_counter = 0;
    bool callback_success = false;
    std::vector<sample_type> buff(packet_size);

    while (not stop_signal_called and not callback_success)
    {
        uhd::rx_metadata_t md;
        size_t num_curr_rx_samps = rx_streamer->recv(&buff.front(), packet_size, md, timeout, false);

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
        else if (num_curr_rx_samps < packet_size)
        {
            LOG_WARN_FMT("Only %1% samples out of requested %2% samples received in round %3%!", num_curr_rx_samps, packet_size, rx_counter);
        }

        if (not success)
            break;

        auto packet_timer = md.time_spec;

        callback_success = callback(buff, num_curr_rx_samps, md.time_spec);

        std::cout << "\rNum of packets received so far = " << rx_counter;
        std::cout.flush();
        ++rx_counter;
    }

    if (stream_cmd.stream_mode == uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS)
    {
        stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
        rx_streamer->issue_stream_cmd(stream_cmd);
    }

    std::cout << std::endl;
}

bool USRP_class::cycleStartDetector(bool &stop_signal_called, uhd::time_spec_t &ref_timer, float &ref_sig_power, const float &max_duration)
{
    size_t max_num_samples = size_t(max_duration * rx_rate);
    size_t num_samples_processed = 0;
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    size_t ex_save_mul = 1; // additional set of samples captured after successful detection

    size_t capacity = N_zfc * (reps_zfc + ex_save_mul);
    std::deque<sample_type> saved_P(capacity);
    std::vector<sample_type> saved_buffer(2 * N_zfc, sample_type(0.0)); // samples from previous packet

    bool buffer_init = false, detection_flag = false;
    int save_extra = ex_save_mul * N_zfc, extra = 0, counter = 0;
    sample_type P(0.0);
    float R = 0.0;
    float M = 0;
    float M_threshold = 0.01;
    uhd::time_spec_t ref_end_timer(0.0);

    bool successful_csd = false;

    std::function schmidt_cox = [&](const std::vector<sample_type> &rx_stream, const size_t &rx_stream_size, const uhd::time_spec_t &rx_timer)
    {
        sample_type samp_1(0.0), samp_2(0.0), samp_3(0.0);

        for (int i = 0; i < rx_stream_size; ++i)
        {
            if (i < 2 * N_zfc)
                samp_1 = saved_buffer[i];
            else
                samp_1 = rx_stream[i - 2 * N_zfc];

            if (i < N_zfc)
                samp_2 = saved_buffer[i + N_zfc];
            else
                samp_2 = rx_stream[i - N_zfc];

            samp_3 = rx_stream[i];

            P = P + (std::conj(samp_2) * samp_3) - (std::conj(samp_1) * samp_2);

            if (buffer_init)
                R = R + std::norm(samp_3) - std::norm(samp_2);
            else
            {
                if (i < 2 * N_zfc)
                    R = R + std::norm(samp_3);
                else
                    buffer_init = true;
            }

            M = std::norm(P) / std::max(R, float(1e-6));

            if (M > M_threshold)
            {
                // LOG_INFO_FMT("UP -- (%4%) |P|^2 = %1%, R = %2%, M = %3%", std::norm(P), R, M, i);
                saved_P.pop_front();
                saved_P.push_back(P);

                if (not detection_flag)
                    detection_flag = true;

                ++counter;
            }
            else
            {
                if (detection_flag)
                {
                    if ((counter < N_zfc * (reps_zfc - 1)) or (counter > N_zfc * (reps_zfc + ex_save_mul)))
                    {
                        LOG_DEBUG_FMT("Resetting counter for detection! Counter = %1%", counter);
                        detection_flag = false;
                        saved_P.clear();
                        saved_P.resize(capacity);
                        counter = 0;
                        continue;
                    }

                    // LOG_INFO_FMT("DOWN -- (%4%) |P|^2 = %1%, R = %2%, M = %3%", std::norm(P), R, M, i);
                    saved_P.pop_front();
                    saved_P.push_back(P);
                    if (extra > save_extra)
                    {
                        int ref_end = (i - counter - save_extra) + std::floor(counter / 2) + int(std::floor(N_zfc * reps_zfc / 2) + N_zfc);
                        LOG_DEBUG_FMT("Ref end index = %1%, conuter = %2%", ref_end, counter);
                        ref_end_timer = rx_timer + uhd::time_spec_t(double(ref_end / rx_rate));
                        successful_csd = true;
                        return true;
                    }
                    else
                        ++extra;
                }
            }

            if (i >= rx_stream.size() - 2 * N_zfc)
                saved_buffer[i - (rx_stream.size() - 2 * N_zfc)] = samp_3;
        }

        num_samples_processed += rx_stream.size();
        // LOG_INFO_FMT("(%4%) |P|^2 = %1%, R = %2%, M = %3%", std::norm(P), R, M, num_samples_saved);
        if (num_samples_processed < max_num_samples)
            return false;
        else
            return true;
    };

    receive_continuously_with_callback(stop_signal_called, schmidt_cox);

    if (successful_csd)
    {
        LOG_INFO_FMT("REF timer = %1%, Current timer = %2%", ref_end_timer.get_tick_count(rx_rate), get_time_now().get_tick_count(rx_rate));
        // CFO estimation
        int ref_start_index = saved_P.size() - (save_extra + std::floor(counter / 2) + int(std::floor(N_zfc * (reps_zfc - 1) / 2)));
        LOG_DEBUG_FMT("Start index of ref = %1%", ref_start_index);
        std::vector<sample_type> ex_vec;
        ex_vec.insert(ex_vec.begin(), saved_P.begin() + ref_start_index, saved_P.begin() + ref_start_index + N_zfc * (reps_zfc - 1));
        std::vector<double> phases = unwrap(ex_vec);
        double cfo_mean = std::accumulate(phases.begin(), phases.end(), 0.0) / phases.size() / N_zfc;
        LOG_INFO_FMT("Mean CFO = %1%", cfo_mean);

        ref_timer = ref_end_timer;
        update_cfo(cfo_mean);
        return true;
    }
    else
        return false;
}

void USRP_class::lowpassFiltering(const std::vector<sample_type> &rx_samples, std::vector<sample_type> &decimated_samples)
{
    size_t rx_stream_size = rx_samples.size();
    // Downsampling filter
    size_t decimation_factor = parser.getValue_int("sampling-factor");
    if (decimation_factor != 10)
    {
        LOG_ERROR("ERROR: Only support low-pass filtering with decimation factor = 10.");
        return;
    }
    std::vector<float> fir_filter;
    std::ifstream filter_file("../config/filters/fir_order_51_downscale_10.csv");
    float value;
    // Check if the file was opened successfully
    if (!filter_file)
    {
        LOG_WARN("Error: Could not open the file.");
        return;
    }
    // Read the file line by line
    while (filter_file >> value)
        fir_filter.push_back(value); // Store each float in the vector

    filter_file.close(); // Close the file

    size_t filter_len = fir_filter.size();
    std::vector<sample_type> tail_samples(filter_len * decimation_factor, sample_type(0.0));

    for (int i = 0; i < rx_stream_size; i += decimation_factor)
    {
        // downsample via polyphase filter
        float realpart = 0.0, imagpart = 0.0;
        for (int j = 0; j < filter_len; ++j)
        {
            int signal_index = i - j * decimation_factor;
            if (signal_index < 0 && signal_index + tail_samples.size() > 0)
            {
                realpart += tail_samples[signal_index + tail_samples.size()].real() * fir_filter[j];
                imagpart += tail_samples[signal_index + tail_samples.size()].imag() * fir_filter[j];
            }
            else if (signal_index >= 0 && signal_index <= rx_stream_size)
            {
                realpart += rx_samples[signal_index].real() * fir_filter[j];
                imagpart += rx_samples[signal_index].imag() * fir_filter[j];
            }
            else
                LOG_WARN_FMT("signal_index %1% is invalid!!", signal_index);
        }

        sample_type sample_dw(realpart, imagpart);
        decimated_samples.emplace_back(sample_dw);
    }
}