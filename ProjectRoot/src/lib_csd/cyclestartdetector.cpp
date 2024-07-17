#include "cyclestartdetector.hpp"

CycleStartDetector::CycleStartDetector(
    ConfigParser &parser,
    size_t &capacity,
    const uhd::time_spec_t &rx_sample_duration,
    PeakDetectionClass &peak_det_obj) : parser(parser),
                                        synced_buffer(capacity),
                                        rx_sample_duration(rx_sample_duration),
                                        peak_det_obj_ref(peak_det_obj),
                                        samples_buffer(),
                                        timer(),
                                        fftw_wrapper()
{
    prev_timer = uhd::time_spec_t(0.0);
    N_zfc = parser.getValue_int("Ref-N-zfc");
    m_zfc = parser.getValue_int("Ref-m-zfc");
    R_zfc = parser.getValue_int("Ref-R-zfc");
    size_t max_rx_packet_size = parser.getValue_int("max-rx-packet-size");
    // capacity = std::pow(2.0, parser.getValue_int("capacity-pow"));
    if (capacity <= max_rx_packet_size)
        LOG_ERROR("Buffer capacity must be greater than maximum receive buffer size.");

    corr_seq_len = N_zfc * parser.getValue_int("corr-seq-len-mul");

    samples_buffer.resize(corr_seq_len + N_zfc - 1);
    timer.resize(corr_seq_len);

    WaveformGenerator wf_gen = WaveformGenerator();
    wf_gen.initialize(wf_gen.ZFC, N_zfc, 1, 0, 0, m_zfc, 1.0, 0);
    zfc_seq = wf_gen.generate_waveform();

    // FFT length
    while (fft_L < corr_seq_len + N_zfc - 1)
    {
        fft_L *= 2;
    }
    fftw_wrapper.initialize(fft_L);
    std::vector<std::complex<float>> padded_zfc;
    fftw_wrapper.zeroPad(zfc_seq, padded_zfc, fft_L);
    fftw_wrapper.fft(padded_zfc, zfc_seq_fft_conj);
    for (auto &val : zfc_seq_fft_conj)
    {
        val = std::conj(val);
    }

    update_noise_level = (parser.getValue_str("update-noise-level") == "true") ? true : false;

    if (capacity < corr_seq_len)
    {
        LOG_WARN_FMT("Capacity '%1%' < consumed data length '%2%'!"
                     "Consider increasing 'capacity_mul' in config, or reducing 'N_zfc'.",
                     capacity, corr_seq_len);
    }

    // debug
    saved_ref.resize(N_zfc * R_zfc * 2);
};

void CycleStartDetector::reset()
{
    synced_buffer.reset();
    prev_timer = uhd::time_spec_t(0.0);
    peak_det_obj_ref.reset();
}

void CycleStartDetector::produce(const std::vector<std::complex<float>> &samples, const size_t &samples_size, const uhd::time_spec_t &packet_start_time, bool &stop_signal_called)
{
    // insert first timer
    uhd::time_spec_t next_time = packet_start_time; // USRP time of first packet

    // insert samples into the buffer
    for (size_t i = 0; i < samples_size; ++i)
    {
        while (!synced_buffer.push(samples[i], next_time))
        {
            if (stop_signal_called)
                break;
            // LOG_DEBUG("Yield Producer");
            std::this_thread::yield();
        }
        next_time += rx_sample_duration;
    }
}

void CycleStartDetector::consume(std::atomic<bool> &csd_success_signal, bool &stop_signal_called)
{

    if (peak_det_obj_ref.detection_flag)
    {
        csd_tx_start_timer = get_wait_time(parser.getValue_float("start-tx-wait-microsec"));
        LOG_INFO_FMT("Transmission is timed in %1% secs", csd_tx_start_timer.get_real_secs());
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
        for (int i = 0; i < corr_seq_len; ++i)
        {
            std::complex<float> sample;
            while (!synced_buffer.pop(sample, timer[i]))
            {
                if (stop_signal_called)
                    break;
                // LOG_DEBUG("Yield Consumer");
                std::this_thread::yield();
            }
            // insert data into deque buffer
            samples_buffer.pop_front();
            samples_buffer.push_back(sample);
        }

        std::vector<std::complex<float>> corr_result;
        fft_cross_correlate(samples_buffer, corr_result);
        peak_detector(corr_result, timer);

        // correlation_operation(samples_buffer, timer);

        // auto end = std::chrono::high_resolution_clock::now();
        // size_t duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "\r # samples without peak = " << num_samples_without_peak << ". Max peak-to-noise-ratio = " << max_pnr << std::flush;
        // << ". Duration of 'correlation_operation' = " << duration << " microsecs, frame duration = " << size_t(corr_seq_len / parser.getValue_float("rate") * 1e6) << " microsecs. \t" << std::flush;
    }
}

void CycleStartDetector::fft_cross_correlate(const std::deque<std::complex<float>> &samples, std::vector<std::complex<float>> &result)
{
    std::vector<std::complex<float>> padded_samples, fft_samples, product(fft_L), ifft_result;
    fftw_wrapper.zeroPad(samples, padded_samples, fft_L);
    fftw_wrapper.fft(padded_samples, fft_samples);

    for (int i = 0; i < fft_L; ++i)
    {
        product[i] = fft_samples[i] * zfc_seq_fft_conj[i];
    }

    fftw_wrapper.ifft(product, ifft_result);

    result.resize(corr_seq_len);
    for (int i = 0; i < corr_seq_len; ++i)
    {
        result[i] = ifft_result[i];
    }
}

void CycleStartDetector::peak_detector(const std::vector<std::complex<float>> &corr_results, const std::vector<uhd::time_spec_t> &timer)
{
    // Perform cross-correlation
    bool found_peak = false;
    float sum_ampl = 0.0;
    float corr_abs_val = 0.0;
    float curr_pnr = 0.0;

    for (int i = 0; i < corr_seq_len; ++i)
    {
        std::complex<float> corr = corr_results[i];
        corr_abs_val = std::abs(corr) / N_zfc;
        curr_pnr = corr_abs_val / peak_det_obj_ref.noise_level;

        // debug
        if (curr_pnr > max_pnr)
            max_pnr = curr_pnr;

        if (curr_pnr >= peak_det_obj_ref.curr_pnr_threshold)
        {
            found_peak = true;
            std::cout << std::endl;
            peak_det_obj_ref.process_corr(corr, timer[i]);
            num_samples_without_peak = 0;
        }
        else
        {
            found_peak = false;
            if (update_noise_level)
                sum_ampl += corr_abs_val;

            if (num_samples_without_peak == std::numeric_limits<size_t>::max())
                num_samples_without_peak = 0; // reset
            ++num_samples_without_peak;
        }

        //  debug
        saved_ref.pop_front();
        saved_ref.push_back(corr_results[i]); // save the last sample of the corr seq

        if (peak_det_obj_ref.detection_flag)
        {
            // debug -- save data to a file for later analysis
            std::ofstream outfile;
            std::vector<std::complex<float>> vec_saved_ref(saved_ref.begin(), saved_ref.end());
            save_stream_to_file(saved_ref_filename, outfile, vec_saved_ref);
            saved_ref.clear();
            saved_ref.resize(N_zfc * R_zfc * 2);

            // break the for loop
            break;
        }
        else // increase conuter
            peak_det_obj_ref.increase_samples_counter();
    }

    // udpate noise level
    if ((not found_peak) and update_noise_level and (not peak_det_obj_ref.detection_flag))
        peak_det_obj_ref.updateNoiseLevel(sum_ampl / corr_seq_len, corr_seq_len);
}

void CycleStartDetector::correlation_operation(const std::vector<std::complex<float>> &samples, const std::vector<uhd::time_spec_t> &timer)
{
    // Perform cross-correlation
    bool found_peak = false;
    float sum_ampl = 0.0;
    float corr_abs_val = 0.0;
    float curr_pnr = 0.0;

    for (int i = 0; i < corr_seq_len; ++i)
    {
        // compute correlation
        std::complex<float> corr(0.0, 0.0);
        for (uint16_t j = 0; j < N_zfc; ++j)
            corr += (samples[i + j] * std::conj(zfc_seq[j]));

        corr_abs_val = std::abs(corr) / N_zfc;
        curr_pnr = corr_abs_val / peak_det_obj_ref.noise_level;

        // debug
        if (curr_pnr > max_pnr)
            max_pnr = curr_pnr;

        if (curr_pnr >= peak_det_obj_ref.curr_pnr_threshold)
        {
            found_peak = true;
            std::cout << std::endl;
            peak_det_obj_ref.process_corr(corr, timer[i]);
            num_samples_without_peak = 0;
        }
        else
        {
            found_peak = false;
            if (update_noise_level)
                sum_ampl += corr_abs_val;

            if (num_samples_without_peak == std::numeric_limits<size_t>::max())
                num_samples_without_peak = 0; // reset
            ++num_samples_without_peak;
        }

        //  debug
        saved_ref.pop_front();
        saved_ref.push_back(samples[i + N_zfc - 1]); // save the last sample of the corr seq

        if (peak_det_obj_ref.detection_flag)
        {
            // debug -- save data to a file for later analysis
            std::ofstream outfile;
            std::vector<std::complex<float>> vec_saved_ref(saved_ref.begin(), saved_ref.end());
            save_stream_to_file(saved_ref_filename, outfile, vec_saved_ref);
            saved_ref.clear();
            saved_ref.resize(N_zfc * R_zfc * 2);

            // break the for loop
            break;
        }
        else // increase conuter
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