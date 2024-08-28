#ifndef CSD_CLASS
#define CSD_CLASS

#include "pch.hpp"
#include "log_macros.hpp"
#include "config_parser.hpp"
#include "circular_buffer.hpp"
#include "peakdetector.hpp"
#include "utility.hpp"
#include "FFTWrapper.hpp"
#include "waveforms.hpp"

class CycleStartDetector
{
public:
    CycleStartDetector(PeakDetectionClass &peak_det_obj, ConfigParser &parser, size_t &capacity, const uhd::time_spec_t &rx_sample_duration);

    void produce(const std::vector<samp_type> &samples, const size_t &samples_size, const uhd::time_spec_t &time, bool &stop_signal_called);

    void consume(std::atomic<bool> &csd_success_signal, bool &stop_signal_called);

    uhd::time_spec_t get_wait_time();

    uhd::time_spec_t csd_tx_start_timer;
    float est_ref_sig_amp, tx_wait_microsec, calibration_ratio;
    double cfo;
    bool is_correct_cfo;
    size_t cfo_counter, cfo_count_max = std::numeric_limits<size_t>::max();
    size_t save_ref_len;

    // debug
    std::string saved_ref_filename = "";

    size_t num_samples_without_peak = 0;

private:
    CircularBuffer packets;

    std::deque<samp_type> samples_buffer;
    std::vector<uhd::time_spec_t> timer;

    std::deque<samp_type> saved_ref;
    std::deque<uhd::time_spec_t> saved_ref_timer;

    uhd::time_spec_t prev_timer;

    ConfigParser parser;
    uhd::time_spec_t rx_sample_duration;
    PeakDetectionClass peak_det_obj_ref;

    void reset();
    float est_e2e_ref_sig_amp();

    size_t N_zfc, m_zfc, R_zfc;
    size_t corr_seq_len;
    size_t capacity;

    std::vector<samp_type> zfc_seq;

    void post_peak_det();
    void update_peaks_info(const float &new_cfo);

    // FFT related
    size_t fft_L = 1, fft_LL = 1;
    FFTWrapper fftw_wrapper, fftw_wrapper_LL;
    std::vector<samp_type> zfc_seq_fft_conj, zfc_seq_fft_conj_LL;
    std::vector<samp_type> fft_cross_correlate(const std::deque<samp_type> &samples);
    std::vector<samp_type> fft_cross_correlate_LL(const std::deque<samp_type> &samples);
    void peak_detector(const std::vector<samp_type> &corr_results, const std::vector<uhd::time_spec_t> &timer);

    bool update_noise_level = false;

    float max_pnr = 0.0;
};

#endif // CSD_CLASS