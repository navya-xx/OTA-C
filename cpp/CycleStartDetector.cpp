#include "CycleStartDetector.hpp"

extern const bool DEBUG;

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

    WaveformGenerator wf_gen;
    zfc_seq = wf_gen.generate_waveform(wf_gen.ZFC, N_zfc, 1, 0, m_zfc, 1.0, 0, false);

    size_t max_rx_packet_size = parser.getValue_int("max-rx-packet-size");
    corr_seq_len = N_zfc * parser.getValue_int("num-corr-size-mul");
    capacity = max_rx_packet_size * parser.getValue_int("capacity-mul");

    if (parser.getValue_str("update-noise-level") == "true")
        update_noise_level = true;

    if (capacity < corr_seq_len + N_zfc)
        throw std::range_error("Capacity < consumed data length (= Ref-N-zfc * 2)!");

    samples_buffer.resize(capacity, std::complex<float>(0.0, 0.0));
    timer.resize(capacity, uhd::time_spec_t(0.0));
};

void CycleStartDetector::reset()
{
    num_produced = 0;
    front = 0;
    rear = 0;

    // samples_buffer.clear();
    samples_buffer.insert(samples_buffer.begin(), capacity, std::complex<float>(0.0, 0.0));
    timer.insert(timer.begin(), capacity, uhd::time_spec_t(0.0));
    prev_timer = uhd::time_spec_t(0.0);
}

void CycleStartDetector::produce(const std::vector<std::complex<float>> &samples, const size_t &samples_size, const uhd::time_spec_t &time)
{
    boost::unique_lock<boost::mutex> lock(mtx);

    cv_producer.wait(lock, [this, &samples_size]
                     { return (capacity - num_produced >= samples_size); }); // Wait for enough space to produce

    // insert first timer
    uhd::time_spec_t next_time = time; // USRP time of first packet

    // std::cout << "\t\t --> Produce ---" << std::endl;
    // insert samples into the buffer
    for (const auto &sample : samples)
    {
        samples_buffer[rear] = sample;
        timer[rear] = next_time; // store absolute sample times

        rear = (rear + 1) % capacity;
        next_time += rx_sample_duration;
    }

    // std::cout << "\t\t --> Produce -- Done ---" << std::endl;

    num_produced += samples_size;

    // Notify consumer
    cv_consumer.notify_one();
}

bool CycleStartDetector::consume(std::atomic<bool> &csd_success_signal)
{
    boost::unique_lock<boost::mutex> lock(mtx);

    // Wait until min_num_produced samples are produced by producer
    cv_consumer.wait(lock, [this, &csd_success_signal]
                     { return (num_produced >= corr_seq_len + N_zfc) and (not csd_success_signal); });

    if (not peak_det_obj_ref.detection_flag)
        correlation_operation();

    if (peak_det_obj_ref.detection_flag)
    {
        csd_tx_start_timer = get_wait_time(parser.getValue_float("tx-wait-microsec"));
        e2e_est_ref_sig_amp = est_e2e_ref_sig_amp();

        // reset corr and peak det objects
        reset();
        peak_det_obj_ref.reset();

        csd_success_signal = true;
        cv_producer.notify_one();
        return true;
    }
    else
    {
        front = (front + corr_seq_len) % capacity;
        num_produced = std::max((num_produced - corr_seq_len), size_t(0));
        cv_producer.notify_one();
        return false;
    }
}

void CycleStartDetector::correlation_operation()
{
    // Perform cross-correlation
    bool found_peak = false;
    float sum_ampl = 0.0;
    float abs_val = 0.0;

    for (size_t i = 0; i < corr_seq_len; ++i)
    {
        // compute correlation
        std::complex<float> corr(0.0, 0.0);
        for (size_t j = 0; j < N_zfc; ++j)
            corr += (samples_buffer[(front + i + j) % capacity] * std::conj(zfc_seq[j]));

        found_peak = peak_det_obj_ref.process_corr(corr, timer[(front + i) % capacity]);

        abs_val = std::abs(corr) / N_zfc;
        // peak_det_obj_ref.save_float_data_into_buffer(abs_val);
        peak_det_obj_ref.save_complex_data_into_buffer(samples_buffer[(front + i) % capacity]);

        if (update_noise_level)
            sum_ampl += abs_val;

        if (peak_det_obj_ref.detection_flag)
            break;
        else
            ++peak_det_obj_ref.samples_from_first_peak;
    }

    // udpate noise level
    if ((not found_peak) and update_noise_level and (not peak_det_obj_ref.detection_flag))
        peak_det_obj_ref.updateNoiseLevel(sum_ampl / corr_seq_len, corr_seq_len);
}

float CycleStartDetector::est_e2e_ref_sig_amp()
{
    // float max_val = 0.0;
    // if (peak_det_obj_ref.is_save_buffer_complex)
    // {
    //     std::vector<std::complex<float>> zfc_rep(N_zfc * (R_zfc - 1));

    //     for (int i = 0; i < R_zfc - 1; ++i)
    //     {
    //         std::copy(zfc_seq.begin(), zfc_seq.end(), zfc_rep.begin() + i * N_zfc);
    //     }

    //     // correlation
    //     max_val = 0.0;
    //     float curr_val = 0.0;
    //     for (int i = 0; i < peak_det_obj_ref.save_buffer_complex.size() - zfc_rep.size(); ++i)
    //     {
    //         std::complex<float> corr(0.0, 0.0);
    //         for (int j = 0; j < zfc_rep.size(); ++j)
    //         {
    //             corr += (peak_det_obj_ref.save_buffer_complex[i + j] * std::conj(zfc_rep[j]));
    //         }
    //         curr_val = std::abs(corr) / zfc_rep.size();
    //         if (max_val < curr_val)
    //             max_val = curr_val;
    //     }
    // }

    float e2e_ref_sig_ampl_1 = peak_det_obj_ref.avg_of_peak_vals();
    // float e2e_ref_sig_ampl_2 = peak_det_obj_ref.est_ch_pow_from_capture_ref_sig();
    std::cout << std::endl
              << "\t\t -> Est. ch-pow 1 = " << e2e_ref_sig_ampl_1 << std::endl
              //   << "\t\t -> Est. ch-pow 2 = " << e2e_ref_sig_ampl_2 << std::endl
              << std::endl;

    return e2e_ref_sig_ampl_1;
}

// void CycleStartDetector::capture_ch_est_seq()
// {
//     size_t num_samps_capture = std::min(min_num_produced, ch_est_samps_size - ch_est_samps_it);
//     std::cout << "Entering capture_ch_est_seq, num_samps_capture = " << num_samps_capture << std::endl;

//     for (size_t i = 0; i < num_samps_capture; ++i)
//     {
//         ch_est_samps.pop_front();
//         ch_est_samps.push_back(samples_buffer[(front + i) % capacity]);
//         peak_det_obj_ref.save_complex_data_into_buffer(samples_buffer[(front + i) % capacity]);
//     }
//     ch_est_samps_it += num_samps_capture;

//     if (ch_est_samps_it >= ch_est_samps_size)
//         ch_est_done = true;
// }

// float CycleStartDetector::est_e2e_ref_sig_amp()
// {
//     size_t N = ch_seq_len;
//     size_t M = parser.getValue_int("ch-seq-M");
//     auto ch_zfc_seq = generateZadoffChuSequence(N, M);
//     float max_val = 0.0;
//     float curr_val = 0.0;
//     for (size_t i = 0; i < ch_est_samps.size() - N; ++i)
//     {
//         std::complex<float> corr(0.0, 0.0);
//         for (size_t j = 0; j < N; ++j)
//         {
//             corr += (ch_est_samps[i + j] * std::conj(ch_zfc_seq[j]));
//         }
//         curr_val = std::abs(corr) / N;
//         if (max_val < curr_val)
//             max_val = curr_val;
//     }

//     if (false)
//     { // save data to file
//         std::ofstream choutfile("../storage/ch_samps_capture.dat", std::ios::out | std::ios::binary);

//         // Check if the file was opened successfully
//         if (!choutfile.is_open())
//         {
//             std::cerr << "Error: Could not open file for writing." << std::endl;
//         }

//         size_t size = ch_est_samps.size();
//         choutfile.write(reinterpret_cast<char *>(&size), sizeof(size));

//         // Write each complex number (real and imaginary parts)
//         for (const auto &complex_value : ch_est_samps)
//         {
//             float real_val = complex_value.real();
//             float complex_val = complex_value.imag();
//             choutfile.write(reinterpret_cast<char *>(&real_val), sizeof(complex_value.real()));
//             choutfile.write(reinterpret_cast<char *>(&complex_val), sizeof(complex_value.imag()));
//         }
//         choutfile.close();
//     }

//     ch_est_samps.clear();
//     ch_est_samps.resize(ch_est_samps_size, std::complex<float>(0.0, 0.0));
//     ch_est_samps_it = 0;
//     ch_est_done = false;
//     min_num_produced = corr_seq_len + N_zfc;
//     return max_val;
// }

uhd::time_spec_t CycleStartDetector::get_wait_time(float tx_wait_microsec)
{
    float peak_to_last_sample_duration = rx_sample_duration.get_real_secs() * parser.getValue_int("Ref-N-zfc") * parser.getValue_int("sync-with-peak-from-last");
    return peak_det_obj_ref.get_sync_time() + uhd::time_spec_t(peak_to_last_sample_duration + tx_wait_microsec / 1e6);
}