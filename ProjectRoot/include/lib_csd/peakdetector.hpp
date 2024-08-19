#ifndef PEAK_CLASS
#define PEAK_CLASS

#include "pch.hpp"
#include "log_macros.hpp"
#include "config_parser.hpp"
#include "utility.hpp"

class PeakDetectionClass
{
private:
    ConfigParser parser;

    size_t *peak_indices;
    std::complex<float> *corr_samples;
    float *peak_vals;
    uhd::time_spec_t *peak_times;

    size_t total_num_peaks;

    size_t ref_seq_len;
    float pnr_threshold, max_pnr;
    float init_noise_ampl;

    size_t peak_det_tol;
    float max_peak_mul;
    size_t sync_with_peak_from_last;

    size_t reset_counter, max_reset_count;

    bool is_update_pnr_threshold;

    void insertPeak(const std::complex<float> &corr_sample, float &peak_val, const uhd::time_spec_t &peak_time);
    void update_pnr_threshold();
    void updatePrevPeak();
    void removeLastPeak();
    float get_max_peak_val();
    bool check_peaks();

public:
    PeakDetectionClass(ConfigParser &parser, const float &init_noise_ampl);

    size_t peaks_count;
    size_t prev_peak_index;
    float prev_peak_val, curr_pnr_threshold;
    size_t samples_from_first_peak;
    bool detection_flag;

    float noise_ampl;
    long int noise_counter;

    std::complex<float> *get_corr_samples_at_peaks();
    uhd::time_spec_t *get_peak_times();
    void print_peaks_data();

    void reset_peaks_counter();
    void reset();

    void process_corr(const std::complex<float> &abs_corr_val, const uhd::time_spec_t &samp_time);
    void increase_samples_counter();

    void updateNoiseLevel(const float &corr_val, const size_t &num_samps);

    float avg_of_peak_vals();
    uhd::time_spec_t get_sync_time();

    float estimate_phase_drift();
    int updatePeaksAfterCFO(const std::vector<float> &abs_corr_vals, const std::deque<uhd::time_spec_t> &new_timer);
};

#endif // PEAK_CLASS