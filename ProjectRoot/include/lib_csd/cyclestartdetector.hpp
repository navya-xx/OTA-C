#ifndef CSD_CLASS
#define CSD_CLASS

#include "pch.hpp"
#include "log_macros.hpp"
#include "ConfigParser.hpp"
#include "peakdetector.hpp"

class CycleStartDetector
{
public:
    CycleStartDetector(ConfigParser &parser, const uhd::time_spec_t &rx_sample_duration, PeakDetectionClass &peak_det_obj);

    void produce(const std::vector<std::complex<float>> &samples, const size_t &samples_size, const uhd::time_spec_t &time);

    bool consume(std::atomic<bool> &csd_success_signal);

    uhd::time_spec_t get_wait_time(float tx_wait_microsec);

    uhd::time_spec_t csd_tx_start_timer;
    float est_ref_sig_amp;

private:
    std::vector<std::complex<float>> samples_buffer;
    std::vector<uhd::time_spec_t> timer;
    uhd::time_spec_t prev_timer;

    ConfigParser parser;
    uhd::time_spec_t rx_sample_duration;
    PeakDetectionClass peak_det_obj_ref;

    void correlation_operation();
    void reset();
    float est_e2e_ref_sig_amp();

    size_t N_zfc, m_zfc, R_zfc;
    size_t corr_seq_len;
    size_t capacity;

    size_t front;
    size_t rear;
    size_t num_produced;
    std::vector<std::complex<float>> zfc_seq;

    bool update_noise_level = false;

    boost::mutex mtx;
    boost::condition_variable cv_producer;
    boost::condition_variable cv_consumer;
};

#endif // CSD_CLASS