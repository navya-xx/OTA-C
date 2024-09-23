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
                                        cfo_counter(0),
                                        calibration_ratio(1.0)
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
    is_correct_cfo = true;

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

    // OTAC
    otac_window_len = parser.getValue_int("test-signal-len");
    otac_buffer_len = 3 * otac_window_len - 1;
    otac_buffer.resize(otac_buffer_len);
    otac_timer.resize(otac_buffer_len);
    otac_meansqr_threshold = parser.getValue_float("otac-threshold");
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

void CycleStartDetector::reset_otac()
{
    otac_detection_flag = false;
    otac_success_flag = false;
    otac_high_counter = 0;
    otac_buffer.clear();
    otac_buffer.resize(otac_buffer_len);
    otac_timer.clear();
    otac_timer.resize(otac_buffer_len);
}

void CycleStartDetector::post_peak_det()
{
    // update phase drift
    float new_cfo = 0.0;
    if (is_correct_cfo)
        new_cfo = peak_det_obj_ref.estimate_phase_drift();

    cfo += new_cfo; // radians/sample
    // cfo_count_max = rational_number_approximation(cfo / (2 * M_PI));
    LOG_INFO_FMT("Estimated new CFO = %1% rad/sample and current CFO = %2% rad/sample.", new_cfo, cfo);

    // updating peaks after CFO correction
    update_peaks_info(new_cfo);
    peak_det_obj_ref.print_peaks_data();

    // get wait time before transmission
    csd_wait_timer = get_wait_time();
}

void CycleStartDetector::post_otac_det()
{
}

void CycleStartDetector::update_peaks_info(const float &new_cfo)
{
    // correct CFO
    std::deque<std::complex<float>> cfo_corrected_ref(save_ref_len);

    if (is_correct_cfo)
    {
        for (size_t n = 0; n < save_ref_len; ++n)
            cfo_corrected_ref[n] = saved_ref[n] * std::complex<float>(std::cos(new_cfo * n), -std::sin(new_cfo * n));
    }
    else
    {
        for (size_t n = 0; n < save_ref_len; ++n)
            cfo_corrected_ref[n] = saved_ref[n];
    }

    // Calculate cross-corr again
    std::vector<std::complex<float>> cfo_corr_results = fft_post_crosscorr(cfo_corrected_ref);

    // find correct peaks
    std::vector<float> abs_corr(save_ref_len);
    for (size_t n = 0; n < save_ref_len; ++n)
        abs_corr[n] = std::abs(cfo_corr_results[n]);

    int ref_start_index = peak_det_obj_ref.updatePeaksAfterCFO(abs_corr, saved_ref_timer);
    LOG_INFO_FMT("ref_start_index %1%", ref_start_index);
    if (ref_start_index + N_zfc * R_zfc > save_ref_len)
        LOG_WARN("detected ref_start_index is incorrect");

    est_ref_sig_pow = calc_signal_power(cfo_corrected_ref, ref_start_index, N_zfc * R_zfc);
    // est_ref_sig_pow = sig_power - (peak_det_obj_ref.noise_ampl * peak_det_obj_ref.noise_ampl);
    // est_ref_sig_pow = peak_det_obj_ref.largest_peak_val;
    // est_ref_sig_pow *= est_ref_sig_pow;
    LOG_INFO_FMT("Estimated ref signal power is %1%.", est_ref_sig_pow);

    // debug -- save data to a file for later analysis
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

/**
 * @brief Inserts samples into the synchronized buffer and associates each sample with a timestamp.
 *
 * This function is responsible for producing samples from an input vector of complex floats and
 * inserting them into a synchronized buffer for further processing or transmission. Each sample
 * is associated with a timestamp, starting from the provided `packet_start_time`, and subsequent
 * samples are spaced based on the system's sample duration. The function continuously attempts to
 * push samples into the buffer, yielding control if necessary, until all samples are processed or
 * a stop signal is called.
 *
 * @param samples A vector of complex float samples to be inserted into the synchronized buffer.
 * @param samples_size The number of samples from the `samples` vector to be processed.
 * @param packet_start_time The USRP time representing when the first sample in the packet should
 *                          be associated with.
 * @param stop_signal_called A boolean flag that, if set to `true`, will halt the sample insertion
 *                           process to stop the function gracefully.
 */
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

/**
 * @brief Consumes samples from a synchronized buffer, performs cross-correlation, and detects peaks.
 *
 * This function is responsible for consuming samples from the synchronized buffer, performing
 * cross-correlation, and detecting peaks for signal processing. It adjusts for carrier frequency
 * offset (CFO) if necessary and updates the internal buffers accordingly. If a detection flag is
 * set, the function will reset relevant objects and signal success. Otherwise, it continues
 * consuming data until a stop signal is called.
 *
 * @param csd_success_signal A reference to an atomic boolean that will be set to `true` if the
 *                           function successfully detects a signal (e.g., a peak detection).
 * @param stop_signal_called A reference to a boolean flag that, if set to `true`, will halt
 *                           the function execution, stopping the consumer from further processing.
 */
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

        std::cout << "\r Num samples without peak = " << num_samples_without_peak << std::flush;
    }
}

void CycleStartDetector::consume_otac(std::atomic<bool> &csd_success_signal, bool &stop_signal_called)
{
    if (otac_success_flag)
    {
        post_otac_det();

        // reset corr and peak det objects
        reset_otac();

        csd_success_signal = true;
    }
    else
    {
        for (int i = 0; i < otac_buffer_len - (otac_window_len - 1); ++i)
        {
            std::complex<float> sample;
            while (!synced_buffer.pop(sample, otac_timer[i]))
            {
                if (stop_signal_called)
                    break;
                // LOG_DEBUG("Yield Consumer");
                std::this_thread::yield();
            }

            // insert data into deque buffer
            otac_buffer.pop_front();
            otac_buffer.push_back(sample);
        }

        otac_detector();

        std::cout << "\r Num samples without successful otac signal = " << num_samples_without_peak << std::flush;
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

std::vector<std::complex<float>> CycleStartDetector::fft_post_crosscorr(const std::deque<std::complex<float>> &samples)
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
    bool found_peak = false;
    float sum_ampl = 0.0;
    float corr_abs_val = 0.0;
    float curr_pnr = 0.0;

    for (int i = 0; i < corr_seq_len; ++i)
    {
        std::complex<float> corr = corr_results[i];
        corr_abs_val = std::abs(corr) / N_zfc;
        curr_pnr = corr_abs_val / peak_det_obj_ref.noise_ampl;

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

void CycleStartDetector::otac_detector()
{
    /** Detection procedure is described as follows
     * 1. Loop over the otac_buffer with sliding window
     * 2. If meanSquare norm over the window is greater than threshold, set otac_detection_flag to true, else set otac_high_counter to zero and continue.
     * 3. If otac_detection_flag is true, check if current value is greater than max_ms_value. Update max_ms_value. Increment conuter otac_high_counter.
     * 4. If otac_high_counter > 2*otac_window_len, break and return max_ms_value.
     */
    float max_ms_value = 0.0;
    // compute mean-square over sliding window of signal in the buffer
    // Loop through the signal with a sliding window
    for (size_t i = 0; i < otac_buffer_len; ++i)
    {
        // For each window, compute the sum of squared magnitudes
        float sum = 0.0f;
        for (size_t j = 0; j < otac_window_len; ++j)
        {
            sum += std::norm(otac_buffer[i + j]);
        }
        float meanSquare = sum / otac_window_len;

        // check if meanSquare value is above threshold
        if (meanSquare > otac_meansqr_threshold)
        {
            otac_detection_flag = true;
            num_samples_without_peak = 0;
            if (max_ms_value < meanSquare)
            {
                max_ms_value = meanSquare;
                otac_max_samp_index = i;
            }
            if (otac_high_counter > 2 * otac_window_len)
            {
                otac_success_flag = true;
                break;
            }
            else
                otac_high_counter++;
        }
        else
        {
            otac_detection_flag = false;
            otac_high_counter = 0;
            max_ms_value = 0.0;
            ++num_samples_without_peak;
            continue;
        }
    }
    if (otac_success_flag)
    {
        otac_max_wms_value = max_ms_value;
        // estimate timer
        double otac_signal_duration = rx_sample_duration.get_real_secs() * otac_window_len;
        double wait_timer = otac_signal_duration + (tx_wait_microsec / 1e6);
        otac_sig_start_timer = uhd::time_spec_t(wait_timer) + otac_timer[otac_max_samp_index];
    }
}

float CycleStartDetector::est_e2e_ref_sig_amp()
{
    float e2e_ref_sig_ampl = peak_det_obj_ref.avg_of_peak_vals();
    return e2e_ref_sig_ampl;
}

uhd::time_spec_t CycleStartDetector::get_wait_time()
{
    size_t sycn_with_peak_from_last = parser.getValue_int("sync-with-peak-from-last");
    size_t ref_pad_len = parser.getValue_int("Ref-padding-mul");
    double peak_to_last_sample_duration = rx_sample_duration.get_real_secs() * (N_zfc * (sycn_with_peak_from_last + ref_pad_len));
    auto abs_peak_timer = peak_det_obj_ref.get_sync_time();
    double wait_timer = peak_to_last_sample_duration + (tx_wait_microsec / 1e6);
    return abs_peak_timer + uhd::time_spec_t(wait_timer);
}