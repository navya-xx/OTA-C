#ifndef CSD_CLASS
#define CSD_CLASS

#include "pch.hpp"
#include "log_macros.hpp"
#include "config_parser.hpp"
#include "circular_packet.hpp"
#include "peakdetector.hpp"
#include "utility.hpp"
#include "FFTWrapper.hpp"
#include "waveforms.hpp"

class CycleStartDetector
{
public:
    CycleStartDetector(ConfigParser &parser, PeakDetectionClass &peak_det_obj, size_t &capacity, const float &rx_sample_duration);

    void produce(std::vector<std::complex<float>> &packet, const size_t &packet_true_size, const uhd::time_spec_t &time_first_sample, bool &stop_signal_called);

    void consume(std::atomic<bool> &csd_success_signal, bool &stop_signal_called);

    uhd::time_spec_t get_wait_time();

    uhd::time_spec_t csd_tx_start_timer;
    float est_ref_sig_amp, tx_wait_microsec;
    double cfo;
    bool is_correct_cfo;
    size_t cfo_counter, cfo_count_max = std::numeric_limits<size_t>::max();
    size_t save_ref_len;

    // debug
    std::string saved_ref_filename = "";

    size_t num_samples_without_peak = 0;

private:
    ConfigParser parser;
    float rx_sample_duration;
    PeakDetectionClass peak_det_obj_ref;

    CircularPacket packet_buffer;
    void unload_packet(bool &stop_signal_called);

    std::vector<std::complex<float>> samples_buffer;
    uhd::time_spec_t current_packet_timer, last_packet_timer;
    size_t packet_size;

    std::deque<std::complex<float>> saved_ref;
    uhd::time_spec_t ref_start_timer;

    bool update_noise_level = false;
    size_t N_zfc, m_zfc, R_zfc;
    size_t corr_seq_len;
    size_t capacity;

    std::vector<std::complex<float>> zfc_seq;

    void peak_detector(const std::vector<std::complex<float>> &corr_results);
    void reset();
    void post_peak_det();
    void update_peaks_info(const float &new_cfo);

    // FFT related
    size_t fft_L = 1, fft_LL = 1;
    FFTWrapper fftw_wrapper, fftw_wrapper_LL;
    std::vector<std::complex<float>> zfc_seq_fft_conj, zfc_seq_fft_conj_LL;
    std::vector<std::complex<float>> fft_cross_correlate(const std::vector<std::complex<float>> &samples);
    std::vector<std::complex<float>> fft_cross_correlate_LL(const std::deque<std::complex<float>> &samples);
};

#endif // CSD_CLASS