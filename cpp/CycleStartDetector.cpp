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
    samples_buffer.resize(capacity, std::complex<float>(0.0, 0.0));
    timer.resize(capacity, uhd::time_spec_t(0.0));
    peak_det_obj_ref.resetPeaks();
}

void CycleStartDetector::produce(const std::vector<std::complex<float>> &samples, const size_t &samples_size, const uhd::time_spec_t &time)
{
    boost::unique_lock<boost::mutex> lock(mtx);

    cv_producer.wait(lock, [this, samples_size]
                     { return capacity - num_produced >= samples_size; }); // Wait for enough space to produce

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

    // Notify consumer in the main file
}

bool CycleStartDetector::consume()
{
    boost::unique_lock<boost::mutex> lock(mtx);

    // Wait until correlation with N_zfc length seq for num_samp_corr samples can be computed
    cv_consumer.wait(lock, [this]
                     { return (num_produced >= num_samp_corr + N_zfc); });

    correlation_operation();

    if (peak_det_obj_ref.detection_flag)
    {
        return true;
    }
    else
    {
        front = (front + num_samp_corr + 1) % capacity;
        num_produced -= (num_samp_corr + 1);
        cv_producer.notify_one();
        return false;
    }
}

void CycleStartDetector::correlation_operation()
{
    // Perform cross-correlation
    bool found_peak = false;
    float sum_ampl = 0.0;
    bool update_noise_level = false;

    for (size_t i = 0; i < num_samp_corr; ++i)
    {
        // compute correlation
        std::complex<float> corr(0.0, 0.0);
        for (size_t j = 0; j < N_zfc; ++j)
        {
            corr += samples_buffer[(front + i + j) % capacity] * std::conj(zfc_seq[j]);
        }
        float abs_val = std::abs(corr) / N_zfc;

        found_peak = peak_det_obj_ref.process_corr(abs_val, timer[(front + i) % capacity]);

        if (update_noise_level)
            sum_ampl += abs_val;

        if (found_peak)
        {
            if (DEBUG)
                std::cout << "Found peak at absolute sample index = " << i << std::endl;
        }

        // save to file only if there is at least one peak detected
        // if (peak_det_obj_ref.peaks_count > 0)
        // peak_det_obj_ref.save_into_buffer(samples_buffer[(front + i) % capacity]);
        peak_det_obj_ref.save_into_buffer(corr);

        if (not peak_det_obj_ref.next())
            break;
    }

    // udpate noise level
    if (not found_peak and update_noise_level)
        peak_det_obj_ref.updateNoiseLevel(sum_ampl / num_samp_corr, num_samp_corr);
}

uhd::time_spec_t CycleStartDetector::get_wait_time(float tx_wait_microsec)
{
    float peak_to_last_sample_duration = rx_sample_duration.get_real_secs() * parser.getValue_int("Ref-N-zfc") * parser.getValue_int("sync-with-peak-from-last");
    return peak_det_obj_ref.get_sync_time() + uhd::time_spec_t(peak_to_last_sample_duration + tx_wait_microsec / 1e6);
}