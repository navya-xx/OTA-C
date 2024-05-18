#include "CycleStartDetector.hpp"

extern const bool DEBUG;

CycleStartDetector::CycleStartDetector(
    ConfigParser &parser,
    const uhd::time_spec_t &rx_sample_duration,
    PeakDetectionClass &peak_det_obj) : parser(parser),
                                        rx_sample_duration(rx_sample_duration),
                                        peak_det_obj_ref(peak_det_obj),
                                        prev_timer(uhd::time_spec_t(0.0)),
                                        front(0),
                                        rear(0),
                                        num_produced(0)
{
    N_zfc = parser.getValue_int("Ref-N-zfc");
    m_zfc = parser.getValue_int("Ref-m-zfc");
    R_zfc = parser.getValue_int("Ref-R-zfc");
    zfc_seq = generateZadoffChuSequence(N_zfc, m_zfc);

    size_t max_rx_packet_size = parser.getValue_int("max-rx-packet-size");
    num_samp_corr = N_zfc * parser.getValue_int("num-corr-size-mul");
    capacity = max_rx_packet_size * parser.getValue_int("capacity-mul");
    min_num_produced = num_samp_corr + N_zfc;

    if (parser.getValue_str("update-noise-level") == "true")
        update_noise_level = true;
    else
        update_noise_level = false;

    // ch_est_done = false;
    // ch_seq_len = parser.getValue_int("ch-seq-len");
    // ch_est_samps_size = 5 * ch_seq_len;
    // ch_est_samps_it = 0;
    // ch_est_samps.resize(ch_est_samps_size, std::complex<float>(0.0, 0.0));

    if (capacity < num_samp_corr + N_zfc)
        throw std::range_error("Capacity < consumed data length (= Ref-N-zfc * 2). Consider increasing Ref-N-zfc value!");

    samples_buffer.resize(capacity, std::complex<float>(0.0, 0.0));
    timer.resize(capacity, uhd::time_spec_t(0.0));
};

void CycleStartDetector::reset()
{
    num_produced = 0;
    front = 0;
    rear = 0;
    min_num_produced = num_samp_corr + N_zfc;
    samples_buffer.clear();
    timer.clear();
    samples_buffer.resize(capacity, std::complex<float>(0.0, 0.0));
    timer.resize(capacity, uhd::time_spec_t(0.0));
    prev_timer = uhd::time_spec_t(0.0);
}

void CycleStartDetector::produce(const std::vector<std::complex<float>> &samples, const size_t &samples_size, const uhd::time_spec_t &time, std::atomic<bool> &csd_success_signal)
{
    boost::unique_lock<boost::mutex> lock(mtx);

    // if (csd_success_signal)
    //     std::cout << "CSD success - waiting producer" << std::endl;

    cv_producer.wait(lock, [this, &samples_size, &csd_success_signal]
                     { return (capacity - num_produced >= samples_size); }); // Wait for enough space to produce

    // insert first timer
    uhd::time_spec_t next_time = time; // USRP time of first packet

    // insert samples into the buffer
    for (const auto &sample : samples)
    {
        samples_buffer[rear] = sample;
        timer[rear] = next_time; // store absolute sample times

        rear = (rear + 1) % capacity;
        next_time += rx_sample_duration;
    }

    num_produced += samples_size;

    // Notify consumer
    cv_consumer.notify_one();
}

bool CycleStartDetector::consume(std::atomic<bool> &csd_success_signal)
{
    boost::unique_lock<boost::mutex> lock(mtx);

    // Wait until min_num_produced samples are produced by producer
    cv_consumer.wait(lock, [this, &csd_success_signal]
                     { return (num_produced >= min_num_produced) and (not csd_success_signal); });

    if (not peak_det_obj_ref.detection_flag)
        correlation_operation();

    if (peak_det_obj_ref.detection_flag)
    {
        // if (ch_est_done)
        // {
        csd_tx_start_timer = get_wait_time(parser.getValue_float("tx-wait-microsec"));
        ch_pow = get_ch_power();

        // reset corr and peak det objects
        reset();
        peak_det_obj_ref.resetPeaks();

        csd_success_signal = true;
        cv_producer.notify_one();
        return true;
        // }
        // else
        // {
        //     capture_ch_est_seq();
        //     front = (front + min_num_produced) % capacity;
        //     num_produced = std::max((num_produced - min_num_produced), size_t(0));
        //     min_num_produced = std::min(ch_seq_len, parser.getValue_int("max-rx-packet-size"));
        //     cv_producer.notify_one();
        //     return false;
        // }
    }
    else
    {
        front = (front + num_samp_corr) % capacity;
        num_produced = std::max((num_produced - num_samp_corr), size_t(0));
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

    for (size_t i = 0; i < num_samp_corr; ++i)
    {
        // compute correlation
        std::complex<float> corr(0.0, 0.0);
        for (size_t j = 0; j < N_zfc; ++j)
        {
            corr += (samples_buffer[(front + i + j) % capacity] * std::conj(zfc_seq[j]));
        }

        abs_val = std::abs(corr) / N_zfc;

        found_peak = peak_det_obj_ref.process_corr(abs_val, timer[(front + i) % capacity]);

        // peak_det_obj_ref.save_float_data_into_buffer(abs_val);
        peak_det_obj_ref.save_complex_data_into_buffer(samples_buffer[(front + i) % capacity]);

        if (update_noise_level)
            sum_ampl += abs_val;

        if (not peak_det_obj_ref.next())
        {
            // insert last samples into buffer
            // for (int k = 1; k <= N_zfc; ++k)
            // {
            //     peak_det_obj_ref.save_complex_data_into_buffer(samples_buffer[(front + i + k) % capacity]);
            // }
            break;
        }
    }

    // udpate noise level
    if ((not found_peak) and update_noise_level)
        peak_det_obj_ref.updateNoiseLevel(sum_ampl / num_samp_corr, num_samp_corr);
}

float CycleStartDetector::get_ch_power()
{
    std::vector<std::complex<float>> zfc_rep(N_zfc * (R_zfc - 1));

    for (int i = 0; i < R_zfc - 1; ++i)
    {
        std::copy(zfc_seq.begin(), zfc_seq.end(), zfc_rep.begin() + i * N_zfc);
    }

    // correlation
    float max_val = 0.0;
    float curr_val = 0.0;
    for (int i = 0; i < peak_det_obj_ref.save_buffer_complex.size() - zfc_rep.size(); ++i)
    {
        std::complex<float> corr(0.0, 0.0);
        for (int j = 0; j < zfc_rep.size(); ++j)
        {
            corr += (peak_det_obj_ref.save_buffer_complex[i + j] * std::conj(zfc_rep[j]));
        }
        curr_val = std::abs(corr) / zfc_rep.size();
        if (max_val < curr_val)
            max_val = curr_val;
    }

    float pch_power = peak_det_obj_ref.get_avg_ch_pow();
    std::cout << std::endl
              << "\t\t -> Est. ch-pow (" << pch_power << ", " << max_val << ")" << std::endl;
    return max_val;
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

// float CycleStartDetector::get_ch_power()
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
//     min_num_produced = num_samp_corr + N_zfc;
//     return max_val;
// }

uhd::time_spec_t CycleStartDetector::get_wait_time(float tx_wait_microsec)
{
    float peak_to_last_sample_duration = rx_sample_duration.get_real_secs() * parser.getValue_int("Ref-N-zfc") * parser.getValue_int("sync-with-peak-from-last");
    return peak_det_obj_ref.get_sync_time() + uhd::time_spec_t(peak_to_last_sample_duration + tx_wait_microsec / 1e6);
}