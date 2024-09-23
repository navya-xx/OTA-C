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
    CycleStartDetector(ConfigParser &parser, size_t &capacity, const uhd::time_spec_t &rx_sample_duration, PeakDetectionClass &peak_det_obj);

    void produce(const std::vector<std::complex<float>> &samples, const size_t &samples_size, const uhd::time_spec_t &time, bool &stop_signal_called);

    void consume(std::atomic<bool> &csd_success_signal, bool &stop_signal_called);

    void consume_otac(std::atomic<bool> &csd_success_signal, bool &stop_signal_called);

    uhd::time_spec_t get_wait_time();

    uhd::time_spec_t csd_wait_timer;
    float est_ref_sig_pow, tx_wait_microsec, calibration_ratio;
    double cfo;
    bool is_correct_cfo;
    size_t cfo_counter, cfo_count_max = size_t(2 * M_PI) * std::numeric_limits<size_t>::max() / 10;
    size_t save_ref_len;

    // debug
    std::string saved_ref_filename = "";

    size_t num_samples_without_peak = 0;

    float otac_max_wms_value = 0.0;
    uhd::time_spec_t otac_sig_start_timer;

private:
    SyncedBufferManager<std::complex<float>, uhd::time_spec_t> synced_buffer; // contains both samples_buffer and timer_buffer
    // SyncedBufferManager<std::complex<float>, uhd::time_spec_t> saved_ref;

    std::deque<std::complex<float>> samples_buffer;
    std::vector<uhd::time_spec_t> timer;

    std::deque<std::complex<float>> saved_ref;
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

    std::vector<std::complex<float>> zfc_seq;

    void post_peak_det();
    void update_peaks_info(const float &new_cfo);

    // FFT related
    size_t fft_L = 1, fft_LL = 1;
    FFTWrapper fftw_wrapper, fftw_wrapper_LL;
    std::vector<std::complex<float>> zfc_seq_fft_conj, zfc_seq_fft_conj_LL;
    std::vector<std::complex<float>> fft_cross_correlate(const std::deque<std::complex<float>> &samples);
    std::vector<std::complex<float>> fft_post_crosscorr(const std::deque<std::complex<float>> &samples);
    void peak_detector(const std::vector<std::complex<float>> &corr_results, const std::vector<uhd::time_spec_t> &timer);

    bool update_noise_level = false;
    float max_pnr = 0.0;

    // OTAC related
    bool otac_detection_flag = false, otac_success_flag = false;
    void otac_detector();
    void post_otac_det();
    void reset_otac();
    size_t otac_buffer_len, otac_window_len, otac_high_counter = 0, otac_max_samp_index;
    float otac_meansqr_threshold;
    std::deque<std::complex<float>> otac_buffer;
    std::vector<uhd::time_spec_t> otac_timer;
};

#endif // CSD_CLASS