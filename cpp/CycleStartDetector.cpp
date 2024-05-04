#include "CycleStartDetector.hpp"
#include "utility_funcs.hpp"

extern const bool DEBUG;
extern const size_t PEAK_DETECTION_TOLERANCE;

CycleStartDetector::CycleStartDetector(
    size_t capacity,
    uhd::time_spec_t sample_duration,
    size_t num_samp_corr,
    size_t N_zfc,
    size_t m_zfc,
    size_t R_zfc,
    PeakDetectionClass &peak_det_obj) : samples_buffer(capacity),
                                        timer(capacity),
                                        num_samp_corr(num_samp_corr),
                                        sample_duration(sample_duration),
                                        capacity(capacity),
                                        front(0),
                                        rear(0),
                                        num_produced(0),
                                        N_zfc(N_zfc),
                                        m_zfc(m_zfc),
                                        R_zfc(R_zfc),
                                        zfc_seq(generateZadoffChuSequence(N_zfc, m_zfc)),
                                        peak_det_obj_ref(peak_det_obj){};

void CycleStartDetector::produce(const std::vector<std::complex<float>> &samples, const size_t &samples_size, const uhd::time_spec_t &time)
{
    boost::unique_lock<boost::mutex> lock(mtx);

    cv_producer.wait(lock, [this, samples_size]
                     { return capacity - num_produced >= samples_size; }); // Wait for enough space to produce

    // insert first timer
    uhd::time_spec_t next_time = time;

    // insert samples into the buffer
    for (const auto &sample : samples)
    {
        samples_buffer[rear] = sample;
        timer[rear] = next_time; // store absolute sample times

        rear = (rear + 1) % capacity;
        next_time += sample_duration;
    }

    num_produced += samples_size;

    cv_consumer.notify_one(); // Notify consumer that new data is available
}

bool CycleStartDetector::consume()
{
    boost::unique_lock<boost::mutex> lock(mtx);

    // Wait until correlation with N_zfc length seq for num_samp_corr samples can be computed
    cv_consumer.wait(lock, [this]
                     { return num_produced >= num_samp_corr + N_zfc; });

    correlation_operation();

    if (peak_det_obj_ref.detection_flag)
        return true;
    else
    {
        front = (front + num_samp_corr + 1) % capacity;
        num_produced -= (num_samp_corr + 1);
        cv_producer.notify_one(); // Notify producer that space is available
        return false;
    }
}

void CycleStartDetector::correlation_operation()
{

    // Perform cross-correlation
    for (size_t i = 0; i < num_samp_corr; ++i)
    {

        // compute correlation
        std::complex<float> corr(0.0, 0.0);
        for (size_t j = 0; j < N_zfc; ++j)
        {
            corr += samples_buffer[(front + i + j) % capacity] * std::conj(zfc_seq[j]);
        }

        float abs_val = std::abs(corr) / N_zfc;

        bool found_peak = peak_det_obj_ref.process_corr(abs_val, timer[(front + i) % capacity]);

        if (found_peak)
        {
            if (DEBUG)
                std::cout << "Found peak at absolute sample index = " << i << std::endl;
        }

        // save to file only if there is at least one peak detected
        if (peak_det_obj_ref.peaks_count > 0)
            peak_det_obj_ref.save_into_buffer(samples_buffer[(front + i) % capacity]);

        if (not peak_det_obj_ref.next())
            break;
    }
}
