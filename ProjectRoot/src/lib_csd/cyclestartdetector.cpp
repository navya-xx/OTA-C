#include "cyclestartdetector.hpp"
#include "waveforms.hpp"

CycleStartDetector::CycleStartDetector(
    ConfigParser &parser,
    const uhd::time_spec_t &rx_sample_duration,
    PeakDetectionClass &peak_det_obj) : parser(parser),
                                        rx_sample_duration(rx_sample_duration),
                                        peak_det_obj_ref(peak_det_obj)
{
    prev_timer = uhd::time_spec_t(0.0);
    front = 0;
    rear = 0;
    num_produced = 0;

    N_zfc = parser.getValue_int("Ref-N-zfc");
    m_zfc = parser.getValue_int("Ref-m-zfc");
    R_zfc = parser.getValue_int("Ref-R-zfc");

    WaveformGenerator wf_gen = WaveformGenerator();
    wf_gen.initialize(wf_gen.ZFC, N_zfc, 1, 0, 0, m_zfc, 1.0, 0);
    zfc_seq = wf_gen.generate_waveform();

    size_t max_rx_packet_size = parser.getValue_int("max-rx-packet-size");
    corr_seq_len = N_zfc * parser.getValue_int("corr-seq-len-mul");
    capacity = max_rx_packet_size * parser.getValue_int("capacity-mul");

    update_noise_level = (parser.getValue_str("update-noise-level") == "true") ? true : false;

    if (capacity < corr_seq_len)
    {
        LOG_WARN_FMT("Capacity '%1%' < consumed data length '%2%'!"
                     "Consider increasing 'capacity_mul' in config, or reducing 'N_zfc'.",
                     capacity, corr_seq_len);
    }

    samples_buffer.resize(capacity, std::complex<float>(0.0, 0.0));
    timer.resize(capacity, uhd::time_spec_t(0.0));
};

void CycleStartDetector::reset()
{
    num_produced = 0;
    front = 0;
    rear = 0;

    samples_buffer.clear();
    samples_buffer.resize(capacity, std::complex<float>(0.0, 0.0));
    timer.clear();
    timer.resize(capacity, uhd::time_spec_t(0.0));
    prev_timer = uhd::time_spec_t(0.0);

    peak_det_obj_ref.reset();
}

void CycleStartDetector::produce(const std::vector<std::complex<float>> &samples, const size_t &samples_size, const uhd::time_spec_t &packet_start_time)
{
    boost::unique_lock<boost::mutex> lock(mtx);
    cv_producer.wait(lock, [this, &samples_size]
                     { return (capacity >= samples_size + num_produced); }); // Wait for enough space to produce

    // insert first timer
    uhd::time_spec_t next_time = packet_start_time; // USRP time of first packet

    // insert samples into the buffer
    for (size_t i = 0; i < samples_size; ++i)
    {
        samples_buffer[rear] = samples[i];
        timer[rear] = next_time; // store absolute sample times

        rear = (rear + 1) % capacity;
        next_time += rx_sample_duration;
    }

    num_produced += samples_size;

    // Notify consumer
    cv_consumer.notify_one();
}

void CycleStartDetector::consume(std::atomic<bool> &csd_success_signal)
{
    boost::unique_lock<boost::mutex> lock(mtx);

    // Wait until min_num_produced samples are produced by producer
    // Wait if csd_success_signal is true -- consumer waits for the next round of CSD after success
    cv_consumer.wait(lock, [this, &csd_success_signal]
                     { return (num_produced >= corr_seq_len + (N_zfc - 1)) and (not csd_success_signal); });

    // if (not peak_det_obj_ref.detection_flag)

    if (peak_det_obj_ref.detection_flag)
    {
        csd_tx_start_timer = get_wait_time(parser.getValue_float("start-tx-wait-microsec"));
        LOG_INFO_FMT("Tx_timer is %1%", csd_tx_start_timer.get_real_secs());
        // phase drift
        est_ref_sig_amp = est_e2e_ref_sig_amp();
        estimated_sampling_rate_offset = std::round(peak_det_obj_ref.estimate_phase_drift() / (2 * M_PI));
        remaining_cfo = (peak_det_obj_ref.estimate_phase_drift() / (2 * M_PI) - estimated_sampling_rate_offset) * (2 * M_PI);
        LOG_INFO_FMT("Phase drift %1% and CFO adjustment %2%.", estimated_sampling_rate_offset, remaining_cfo);

        // reset corr and peak det objects

        reset();

        csd_success_signal = true;
    }
    else
    {
        // auto start = std::chrono::high_resolution_clock::now();

        correlation_operation(samples_buffer);
        front = (front + corr_seq_len) % capacity;
        if (num_produced < corr_seq_len)
            LOG_ERROR_FMT("num_produced %1% < corr_seq_len %2%. Should not reach here!!!", num_produced, corr_seq_len);
        num_produced = num_produced - corr_seq_len;

        // auto end = std::chrono::high_resolution_clock::now();
        // size_t duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "\r # samples without peak = " << num_samples_without_peak << ". Max peak-to-noise-ratio = " << max_pnr << std::flush;
        // << ". Duration of 'correlation_operation' = " << duration << " microsecs, frame duration = " << size_t(corr_seq_len / parser.getValue_float("rate") * 1e6) << " microsecs. \t" << std::flush;
    }
    cv_producer.notify_one();
}

void CycleStartDetector::correlation_operation(const std::vector<std::complex<float>> &samples)
{
    // Perform cross-correlation
    bool found_peak = false;
    float sum_ampl = 0.0;
    float abs_val = 0.0;
    float curr_pnr = 0.0;

    for (uint16_t i = 0; i < corr_seq_len; ++i)
    {
        // compute correlation
        std::complex<float> corr(0.0, 0.0);
        for (uint16_t j = 0; j < N_zfc; ++j)
            corr += (samples[(front + i + j) % capacity] * std::conj(zfc_seq[j]));

        abs_val = std::abs(corr) / N_zfc;
        curr_pnr = abs_val / peak_det_obj_ref.noise_level;
        // debug
        if (curr_pnr > max_pnr)
            max_pnr = curr_pnr;

        if (curr_pnr >= peak_det_obj_ref.curr_pnr_threshold)
        {
            found_peak = true;
            std::cout << std::endl;
            peak_det_obj_ref.process_corr(corr, timer[(front + i) % capacity]);
            num_samples_without_peak = 0;
        }
        else
        {
            found_peak = false;
            if (update_noise_level)
                sum_ampl += abs_val;

            if (num_samples_without_peak == std::numeric_limits<size_t>::max())
                num_samples_without_peak = 0; // reset
            ++num_samples_without_peak;
        }

        if (peak_det_obj_ref.detection_flag)
            break;
        else
            peak_det_obj_ref.increase_samples_counter();
    }

    // udpate noise level
    if ((not found_peak) and update_noise_level and (not peak_det_obj_ref.detection_flag))
        peak_det_obj_ref.updateNoiseLevel(sum_ampl / corr_seq_len, corr_seq_len);
}

float CycleStartDetector::est_e2e_ref_sig_amp()
{
    float e2e_ref_sig_ampl = peak_det_obj_ref.avg_of_peak_vals();
    return e2e_ref_sig_ampl;
}

uhd::time_spec_t CycleStartDetector::get_wait_time(float tx_wait_microsec)
{
    float peak_to_last_sample_duration = rx_sample_duration.get_real_secs() * N_zfc * parser.getValue_int("sync-with-peak-from-last");
    return peak_det_obj_ref.get_sync_time() + uhd::time_spec_t(peak_to_last_sample_duration + tx_wait_microsec / 1e6);
}