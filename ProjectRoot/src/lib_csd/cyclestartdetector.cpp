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
                                        fftw_wrapper(),
                                        cfo(0.0),
                                        cfo_counter(0)
{
    prev_timer = uhd::time_spec_t(0.0);
    N_zfc = parser.getValue_int("Ref-N-zfc");
    m_zfc = parser.getValue_int("Ref-m-zfc");
    R_zfc = parser.getValue_int("Ref-R-zfc");

    tx_wait_microsec = parser.getValue_float("start-tx-wait-microsec");

    // capture entire ref signal and more
    save_ref_len = N_zfc * (R_zfc + 2);
    saved_ref.resize(save_ref_len);
    saved_ref_timer.resize(save_ref_len);

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
    int num_FFT_threads = int(parser.getValue_int("num-FFT-threads"));
    fftw_wrapper.initialize(fft_L, num_FFT_threads);
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

    // FFT length
    while (fft_LL < save_ref_len + N_zfc - 1)
    {
        fft_LL *= 2;
    }

    fftw_wrapper_LL.initialize(fft_LL, num_FFT_threads);
    std::vector<std::complex<float>> padded_zfc_LL;
    fftw_wrapper_LL.zeroPad(zfc_seq, padded_zfc_LL, fft_LL);
    fftw_wrapper_LL.fft(padded_zfc_LL, zfc_seq_fft_conj_LL);
    for (auto &val : zfc_seq_fft_conj_LL)
    {
        val = std::conj(val);
    }
};

void CycleStartDetector::reset()
{
    synced_buffer.clear();
    prev_timer = uhd::time_spec_t(0.0);
    peak_det_obj_ref.reset();
    cfo_counter = 0;

    saved_ref.clear();
    saved_ref.resize(save_ref_len);
    saved_ref_timer.clear();
    saved_ref_timer.resize(save_ref_len);
}

void CycleStartDetector::post_peak_det()
{
    // update phase drift
    float new_cfo = peak_det_obj_ref.estimate_phase_drift();
    cfo += new_cfo; // radians/sample
    // cfo_count_max = rational_number_approximation(cfo / (2 * M_PI));
    LOG_INFO_FMT("Estimated new CFO = %1% and current CFO = %2% radians/sample.", new_cfo, cfo);

    // updating peaks after CFO correction
    update_peaks_info(new_cfo);
    peak_det_obj_ref.print_peaks_data();

    // get wait time before transmission
    csd_tx_start_timer = get_wait_time();
    LOG_INFO_FMT("Transmission is timed in %1% secs", csd_tx_start_timer.get_real_secs());
}

void CycleStartDetector::update_peaks_info(const float &new_cfo)
{
    // correct CFO
    std::deque<std::complex<float>> cfo_corrected_ref(save_ref_len);

    for (size_t n = 0; n < save_ref_len; ++n)
        cfo_corrected_ref[n] = saved_ref[n] * std::complex<float>(std::cos(new_cfo * n), -std::sin(new_cfo * n));

    // Calculate cross-corr again
    std::vector<std::complex<float>> cfo_corr_results = fft_cross_correlate_LL(cfo_corrected_ref);

    // find correct peaks
    std::vector<float> abs_corr(save_ref_len);
    for (size_t n = 0; n < save_ref_len; ++n)
        abs_corr[n] = std::abs(cfo_corr_results[n]);

    int ref_start_index = peak_det_obj_ref.updatePeaksAfterCFO(abs_corr, saved_ref_timer);
    LOG_INFO_FMT("ref_start_index %1%", ref_start_index);
    if (ref_start_index + N_zfc * R_zfc > save_ref_len)
        LOG_WARN("detected ref_start_index is incorrect");
    float sig_ampl = 0.0;
    for (int i = 0; i < N_zfc * R_zfc; ++i)
        sig_ampl += std::abs(cfo_corrected_ref[ref_start_index + i]);
    est_ref_sig_amp = sig_ampl / (N_zfc * R_zfc);
    LOG_INFO_FMT("Estimated channel power is %1%.", est_ref_sig_amp);

    // // debug -- save data to a file for later analysis
    if (saved_ref_filename != "")
    {
        std::ofstream outfile;
        // std::vector<std::complex<float>> vec_saved_ref(saved_ref.begin(), saved_ref.end());
        // vec_saved_ref.insert(vec_saved_ref.end(), N_zfc, std::complex<float>(0.0, 0.0));
        // vec_saved_ref.insert(vec_saved_ref.end(), cfo_corrected_ref.begin(), cfo_corrected_ref.end());
        std::vector<std::complex<float>> vec_saved_ref(cfo_corrected_ref.begin(), cfo_corrected_ref.end());
        vec_saved_ref.insert(vec_saved_ref.end(), N_zfc, std::complex<float>(0.0, 0.0));
        vec_saved_ref.insert(vec_saved_ref.end(), cfo_corr_results.begin(), cfo_corr_results.end());
        vec_saved_ref.insert(vec_saved_ref.end(), N_zfc, std::complex<float>(0.0, 0.0));
        LOG_DEBUG_FMT("Saving %1% samples of corrected ref signal and its correlation values to file %2%", vec_saved_ref.size(), saved_ref_filename);
        save_stream_to_file(saved_ref_filename, outfile, vec_saved_ref);
    }
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
        post_peak_det();

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
            // adjust for CFO
            if (cfo != 0.0)
            {
                sample *= std::complex<float>(std::cos(cfo * cfo_counter), -std::sin(cfo * cfo_counter));
                cfo_counter++;
                if (cfo_counter == cfo_count_max)
                    cfo_counter = 0;
            }
            // insert data into deque buffer
            samples_buffer.pop_front();
            samples_buffer.push_back(sample);
        }

        std::vector<std::complex<float>> corr_results = fft_cross_correlate(samples_buffer);
        peak_detector(corr_results, timer);

        // correlation_operation(samples_buffer, timer);

        // auto end = std::chrono::high_resolution_clock::now();
        // size_t duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        // std::cout << "\r # samples without peak = " << num_samples_without_peak << ". Max peak-to-noise-ratio = " << max_pnr << ". Duration of 'correlation_operation' = " << duration << " microsecs, frame duration = " << size_t(corr_seq_len / parser.getValue_float("rate") * 1e6) << " microsecs. \t" << std::flush;
        std::cout << "\r Num samples without peak = " << num_samples_without_peak << std::flush;
    }
}

std::vector<std::complex<float>> CycleStartDetector::fft_cross_correlate(const std::deque<std::complex<float>> &samples)
{
    std::vector<std::complex<float>> padded_samples, fft_samples, product(fft_L), ifft_result;
    fftw_wrapper.zeroPad(samples, padded_samples, fft_L);
    fftw_wrapper.fft(padded_samples, fft_samples);

    for (int i = 0; i < fft_L; ++i)
    {
        product[i] = fft_samples[i] * zfc_seq_fft_conj[i];
    }

    fftw_wrapper.ifft(product, ifft_result);

    std::vector<std::complex<float>> result(ifft_result.begin(), ifft_result.begin() + corr_seq_len);
    return result;
}

std::vector<std::complex<float>> CycleStartDetector::fft_cross_correlate_LL(const std::deque<std::complex<float>> &samples)
{
    std::vector<std::complex<float>> padded_samples, fft_samples, product(fft_LL), ifft_result;
    fftw_wrapper_LL.zeroPad(samples, padded_samples, fft_LL);
    fftw_wrapper_LL.fft(padded_samples, fft_samples);

    for (int i = 0; i < fft_LL; ++i)
    {
        product[i] = fft_samples[i] * zfc_seq_fft_conj_LL[i];
    }

    fftw_wrapper_LL.ifft(product, ifft_result);

    std::vector<std::complex<float>> result(ifft_result.begin(), ifft_result.begin() + save_ref_len);
    return result;
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

        if (peak_det_obj_ref.detection_flag)
        {
            // add N_zfc more samples to the end
            size_t M = 0;
            while ((M + i < corr_seq_len) & (M < N_zfc))
            {
                saved_ref.pop_front();
                saved_ref.push_back(samples_buffer[M + i + N_zfc - 1]);
                saved_ref_timer.pop_front();
                saved_ref_timer.push_back(timer[M + i + N_zfc - 1]);
                M++;
            }

            // break the for loop
            break;
        }
        else // increase conuter
        {
            saved_ref.pop_front();
            saved_ref.push_back(samples_buffer[i + N_zfc - 1]);
            saved_ref_timer.pop_front();
            saved_ref_timer.push_back(timer[i + N_zfc - 1]);
            peak_det_obj_ref.increase_samples_counter();
        }
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

uhd::time_spec_t CycleStartDetector::get_wait_time()
{
    float peak_to_last_sample_duration = rx_sample_duration.get_real_secs() * N_zfc * parser.getValue_int("sync-with-peak-from-last");
    return peak_det_obj_ref.get_sync_time() + uhd::time_spec_t(peak_to_last_sample_duration + tx_wait_microsec / 1e6);
}