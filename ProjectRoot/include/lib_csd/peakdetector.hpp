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
    float *peak_vals;

    size_t total_num_peaks;

    size_t ref_seq_len;
    float pnr_threshold;
    float init_noise_ampl;

    size_t peak_det_tol;
    float max_peak_mul;
    size_t sync_with_peak_from_last;

    size_t reset_counter, max_reset_count;

    bool is_update_pnr_threshold;

    void insertPeak(const float &corr_abs, const size_t &sample_index);
    void update_pnr_threshold();
    void updatePrevPeak();
    void removeLastPeak();

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

    void print_peaks_data();

    void reset_peaks_counter();
    void reset();

    bool process_corr(const float &abs_corr_val, const size_t &sample_index);

    void increase_samples_counter();
    void updateNoiseLevel(const float &corr_val, const size_t &num_samps);

    float estimate_phase_drift();
    int updatePeaksAfterCFO(const std::vector<float> &abs_corr_vals, const std::deque<uhd::time_spec_t> &new_timer);
};

#endif // PEAK_CLASS