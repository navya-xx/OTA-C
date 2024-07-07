#include "peakdetector.hpp"

PeakDetectionClass::PeakDetectionClass(
    ConfigParser &parser,
    const float &init_noise_level) : parser(parser),
                                     detection_flag(false),
                                     init_noise_level(init_noise_level)
{

    ref_seq_len = parser.getValue_int("Ref-N-zfc");
    total_num_peaks = parser.getValue_int("Ref-R-zfc");
    pnr_threshold = parser.getValue_float("pnr-threshold");
    curr_pnr_threshold = pnr_threshold;
    max_pnr = 0.0;

    if (parser.getValue_str("update-pnr-threshold") == "true")
        is_update_pnr_threshold = true;
    else
        is_update_pnr_threshold = false;

    peak_det_tol = parser.getValue_int("peak-det-tol");
    max_peak_mul = parser.getValue_float("max-peak-mul");
    sync_with_peak_from_last = parser.getValue_int("sync-with-peak-from-last");

    peak_indices = new size_t[total_num_peaks];
    peak_vals = new std::complex<float>[total_num_peaks];
    peak_times = new uhd::time_spec_t[total_num_peaks];

    prev_peak_index = 0;
    peaks_count = 0;
    noise_counter = 0;
    noise_level = init_noise_level;
};

std::complex<float> *PeakDetectionClass::get_peak_vals()
{
    return peak_vals;
}

uhd::time_spec_t *PeakDetectionClass::get_peak_times()
{
    return peak_times;
}

void PeakDetectionClass::print_peaks_data()
{
    // Timer analysis
    int num_peaks_detected = peaks_count;
    for (int i = 0; i < num_peaks_detected; ++i)
    {
        LOG_DEBUG_FMT("*PeaksDet* : Peak %1% channel power = %2%", i + 1, std::pow(std::abs(peak_vals[i]), 2));
        if (i < num_peaks_detected - 1)
            LOG_DEBUG_FMT("\t\t Comparing peaks %1% and %2%"
                          " -- Index diff = %3% -- Time diff = %4% microsecs -- Val diff = %5%.",
                          i + 2,
                          i + 1,
                          peak_indices[i + 1] - peak_indices[i],
                          (peak_times[i + 1] - peak_times[i]).get_real_secs() * 1e6,
                          std::abs(peak_vals[i + 1]) - std::abs(peak_vals[i]));
    }
}

float PeakDetectionClass::get_max_peak_val()
{
    float max_val = 0;
    for (int i = 0; i < peaks_count; ++i)
    {
        if (std::abs(peak_vals[i]) > max_val)
            max_val = std::abs(peak_vals[i]);
    }
    return max_val;
}

void PeakDetectionClass::update_pnr_threshold()
{
    if (max_pnr > 0.0)
        curr_pnr_threshold = std::min(std::max(max_peak_mul * prev_peak_val / noise_level, pnr_threshold), max_pnr * max_peak_mul);
}

void PeakDetectionClass::reset()
{
    peaks_count = 0;
    samples_from_first_peak = 0;
    prev_peak_index = 0;
    curr_pnr_threshold = pnr_threshold;
    detection_flag = false;
    noise_level = init_noise_level;
    noise_counter = 0;
}

void PeakDetectionClass::reset_peaks_counter()
{
    peaks_count = 0;
    samples_from_first_peak = 0;
    prev_peak_index = 0;
}

void PeakDetectionClass::insertPeak(const std::complex<float> &peak_val, const uhd::time_spec_t &peak_time)
{
    if (peaks_count == 0) // First peak starts with index 0
        samples_from_first_peak = 0;
    // when more than 2 peaks, check previous registered peaks for correct spacing
    else if (peaks_count > 1 and peaks_count < total_num_peaks)
    {
        size_t reg_peaks_spacing = peak_indices[peaks_count - 1] - peak_indices[peaks_count - 2];

        // if spacing is not as expected, remove all previous peaks except the last registered peak
        if (reg_peaks_spacing > ref_seq_len + peak_det_tol or reg_peaks_spacing < ref_seq_len - peak_det_tol)
        {
            LOG_DEBUG("*PeaksDet* : Peaks spacing incorrect -> Remove all peaks except last.");
            peak_indices[0] = 0;
            peak_vals[0] = peak_vals[peaks_count - 1];
            peak_times[0] = peak_times[peaks_count - 1];
            samples_from_first_peak = samples_from_first_peak - peak_indices[peaks_count - 1];
            peaks_count = 1;
            prev_peak_index = 0;
            // continue to insert the current peak
        }
    }
    // check the last peak -> if at correct spot, return success
    else if (peaks_count == total_num_peaks)
    {
        size_t last_peak_spacing = samples_from_first_peak - peak_indices[peaks_count - 1];
        if (last_peak_spacing > ref_seq_len - peak_det_tol and last_peak_spacing < ref_seq_len + peak_det_tol)
            detection_flag = true;
    }
    // if we reach here, that means something is wrong.
    else
        LOG_WARN("*PeaksDet* : Registered peaks count > total number of peaks."
                 "Should not reach here!");

    if (detection_flag)
    {
        LOG_INFO("*PeaksDet* : Successful detection!");
        print_peaks_data();
        return;
    }
    else
    {
        // fill info for the currently found peak
        peak_indices[peaks_count] = samples_from_first_peak;
        peak_vals[peaks_count] = peak_val;
        peak_times[peaks_count] = peak_time;
    }

    if (is_update_pnr_threshold)
        update_pnr_threshold();

    ++peaks_count;
    prev_peak_index = samples_from_first_peak;
    prev_peak_val = std::abs(peak_val);
}

void PeakDetectionClass::removeLastPeak()
{
    // decrement counter
    --peaks_count;
}

void PeakDetectionClass::updatePrevPeak()
{
    if (peaks_count == 0)
        LOG_WARN("Cannot update peak! No previous peaks found.");
    else
        removeLastPeak();
}

bool PeakDetectionClass::process_corr(const std::complex<float> &corr_val, const uhd::time_spec_t &samp_time)
{
    size_t samples_from_last_peak = samples_from_first_peak - prev_peak_index;

    float abs_corr_val = std::abs(corr_val);

    if ((abs_corr_val / noise_level) > curr_pnr_threshold)
    {
        // First peak
        if (peaks_count == 0)
            insertPeak(corr_val, samp_time);
        else
        {
            // distance of current peak from last
            if (prev_peak_index > samples_from_first_peak)
                LOG_WARN_FMT("*PeakDet* :Previous peak index '%1%' > Number of samples from first peak '%2%'."
                             "This should not happen!",
                             prev_peak_index, samples_from_first_peak);

            // next peak is too far from the last
            // reset peaks and mark this peak as first
            if (samples_from_last_peak > ref_seq_len + peak_det_tol)
            {
                LOG_DEBUG_FMT("*PeakDet* : Next peak is too far from the last. "
                              "Resetting -- samples from last peak '%1%'.",
                              samples_from_last_peak);
                reset_peaks_counter();
                insertPeak(corr_val, samp_time);
            }
            // a higher peak exists in close proximity to last
            // update previous peak
            else if (samples_from_last_peak < ref_seq_len - peak_det_tol)
            {
                // check if this peak is higher than the previous
                if (prev_peak_val < abs_corr_val)
                {
                    LOG_DEBUG_FMT("*PeakDet* : Update previous peak. "
                                  "Last peak val '%1%' is less than current val '%2%'.",
                                  prev_peak_val, abs_corr_val);
                    updatePrevPeak();
                    insertPeak(corr_val, samp_time);
                }
                // else -> do nothing
            }
            // peak found at the right stop
            // insert as a new peak and wait till its updated to the right spot
            else
                insertPeak(corr_val, samp_time);
        }
        // a peak is found
        return true;
    }
    else
    {
        // No peak found
        return false;
    }
}

float PeakDetectionClass::avg_of_peak_vals()
{
    float e2e_est_ref_sig_amp = 0.0;
    float max_peak = get_max_peak_val();

    // average channel power from peak_vals
    for (int i = 0; i < total_num_peaks; ++i)
        e2e_est_ref_sig_amp += std::abs(peak_vals[i]);

    e2e_est_ref_sig_amp = e2e_est_ref_sig_amp / total_num_peaks;

    // update max_pnr
    max_pnr = std::max(max_peak / noise_level * max_peak_mul, pnr_threshold);
    return e2e_est_ref_sig_amp;
}

uhd::time_spec_t PeakDetectionClass::get_sync_time()
{
    return peak_times[peaks_count - sync_with_peak_from_last];
}

float PeakDetectionClass::estimate_freq_offset()
{
    std::vector<std::complex<float>> peak_corr_vals(peak_vals, peak_vals + peaks_count);
    std::vector<double> phases = unwrap(peak_corr_vals); // returns phases between [-pi, pi]
    uhd::time_spec_t init_timer = peak_times[0];
    double init_phase_shift = phases[0];
    double phase_time_prod = 0, time_sqr = 0;
    for (size_t i = 1; i < peaks_count; ++i)
    {
        double timer_diff = (peak_times[i] - init_timer).get_real_secs();
        phase_time_prod += (phases[i] - init_phase_shift) * timer_diff;
        time_sqr += timer_diff * timer_diff;
    }
    double cfo = phase_time_prod / time_sqr;
    return cfo;
}

void PeakDetectionClass::updateNoiseLevel(const float &avg_ampl, const size_t &num_samps)
{
    if (std::abs(avg_ampl - noise_level) / noise_level < 0.1)
    {
        // update noise level by iteratively averaging
        noise_level = (noise_counter * noise_level + avg_ampl * num_samps) / (noise_counter + num_samps);

        if (noise_counter < std::numeric_limits<long>::max())
            noise_counter = noise_counter + num_samps;
        else
            noise_counter = 1; // restart counter
    }
}

bool PeakDetectionClass::check_peaks()
{
    // check if found peaks follow correct gaps
    size_t gap;

    for (int i = 0; i < total_num_peaks - 1; ++i)
    {
        gap = peak_indices[i + 1] - peak_indices[i];
        if (gap < ref_seq_len - peak_det_tol or gap > ref_seq_len + peak_det_tol)
        {
            LOG_DEBUG_FMT("*PeaksDet* : Incorrect peaks spacing between "
                          "peaks %1% at index %3% and %2% at index %4%.",
                          i, i + 1, peak_indices[i], peak_indices[i + 1]);
            return false;
        }
    }

    return true;
}