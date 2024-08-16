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
    peak_vals = new float[total_num_peaks];
    corr_samples = new std::complex<float>[total_num_peaks];
    peak_times = new uhd::time_spec_t[total_num_peaks];

    prev_peak_index = 0;
    peaks_count = 0;
    noise_counter = 0;
    noise_level = init_noise_level;
};

std::complex<float> *PeakDetectionClass::get_corr_samples_at_peaks()
{
    return corr_samples;
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
        LOG_DEBUG_FMT("*PeaksDet* : Peak %1% abs-val/noise = %2%", i + 1, peak_vals[i]);
        if (i < num_peaks_detected - 1)
            LOG_DEBUG_FMT("\t\t Comparing peaks %1% and %2%"
                          " -- Index diff = %3% -- Time diff = %4% microsecs -- Val diff = %5%.",
                          i + 2,
                          i + 1,
                          peak_indices[i + 1] - peak_indices[i],
                          (peak_times[i + 1] - peak_times[i]).get_real_secs() * 1e6,
                          peak_vals[i + 1] - peak_vals[i]);
    }
}

float PeakDetectionClass::get_max_peak_val()
{
    float max_val = 0;
    for (int i = 0; i < peaks_count; ++i)
    {
        if (peak_vals[i] > max_val)
            max_val = peak_vals[i];
    }
    return max_val;
}

void PeakDetectionClass::update_pnr_threshold()
{
    if (is_update_pnr_threshold)
    {
        if (max_pnr > 0.0)
            curr_pnr_threshold = std::min(std::max(max_peak_mul * prev_peak_val, pnr_threshold), max_pnr * max_peak_mul);
        else
            curr_pnr_threshold = std::max(curr_pnr_threshold, std::max(max_peak_mul * prev_peak_val, pnr_threshold));
    }
}

void PeakDetectionClass::reset()
{
    peaks_count = 0;
    samples_from_first_peak = 0;
    prev_peak_index = 0;
    prev_peak_val = 0;
    curr_pnr_threshold = pnr_threshold;
    detection_flag = false;
    noise_level = init_noise_level;
    noise_counter = 0;

    // reset pointers
    delete[] peak_indices;
    delete[] peak_vals;
    delete[] corr_samples;
    delete[] peak_times;
    peak_indices = new size_t[total_num_peaks];
    peak_vals = new float[total_num_peaks];
    corr_samples = new std::complex<float>[total_num_peaks];
    peak_times = new uhd::time_spec_t[total_num_peaks];
}

void PeakDetectionClass::reset_peaks_counter()
{
    peaks_count = 0; // insertPeaks takes care of other variables
}

void PeakDetectionClass::insertPeak(const std::complex<float> &corr_sample, float &peak_val, const uhd::time_spec_t &peak_time)
{
    if (peaks_count == 0) // First peak starts with index 0
        samples_from_first_peak = 0;
    // when more than 2 peaks, check previous registered peaks for correct spacing
    else if (peaks_count > 1 and peaks_count < total_num_peaks - 1)
    {
        const size_t reg_peaks_spacing = peak_indices[peaks_count - 1] - peak_indices[peaks_count - 2];

        // if spacing is not as expected, remove all previous peaks except the last registered peak
        if (reg_peaks_spacing > ref_seq_len + peak_det_tol or reg_peaks_spacing < ref_seq_len - peak_det_tol)
        {
            LOG_DEBUG("*PeaksDet* : Peaks spacing incorrect -> Remove all peaks except last.");
            print_peaks_data();
            peak_indices[0] = 0;
            corr_samples[0] = corr_samples[peaks_count - 1];
            peak_vals[0] = peak_vals[peaks_count - 1];
            peak_times[0] = peak_times[peaks_count - 1];
            samples_from_first_peak = samples_from_first_peak - peak_indices[peaks_count - 1];
            peaks_count = 1;
            prev_peak_index = 0;
            // continue to insert the current peak
        }
    }
    // check the last peak -> if at correct spot, return success
    else if (peaks_count == total_num_peaks - 1)
    {
        const size_t last_peak_spacing = samples_from_first_peak - peak_indices[peaks_count - 1];
        if (last_peak_spacing > ref_seq_len - peak_det_tol and last_peak_spacing < ref_seq_len + peak_det_tol)
            detection_flag = true;
    }
    // if we reach here, that means something is wrong.
    else if (peaks_count > total_num_peaks)
        LOG_WARN("*PeaksDet* : Registered peaks count > total number of peaks."
                 "Should not reach here!");

    // fill info for the currently found peak
    peak_indices[peaks_count] = samples_from_first_peak;
    corr_samples[peaks_count] = corr_sample;
    peak_vals[peaks_count] = peak_val;
    peak_times[peaks_count] = peak_time;
    ++peaks_count;

    if (detection_flag)
    {
        LOG_INFO("*PeaksDet* : Successful detection!");
        print_peaks_data();
        return;
    }

    prev_peak_index = samples_from_first_peak;
    prev_peak_val = peak_val;
    update_pnr_threshold();
}

void PeakDetectionClass::removeLastPeak()
{
    // decrement counter
    --peaks_count; // insertPeak takes care of other variables
}

void PeakDetectionClass::updatePrevPeak()
{
    if (peaks_count == 0)
        LOG_WARN("Cannot update peak! No previous peaks found.");
    else
        removeLastPeak();
}

void PeakDetectionClass::process_corr(const std::complex<float> &corr_sample, const uhd::time_spec_t &samp_time)
{
    const size_t samples_from_last_peak = samples_from_first_peak - prev_peak_index;

    float curr_peak_value = std::abs(corr_sample) / ref_seq_len / noise_level;

    // First peak
    if (peaks_count == 0)
        insertPeak(corr_sample, curr_peak_value, samp_time);
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
            reset();
            insertPeak(corr_sample, curr_peak_value, samp_time);
        }
        // a higher peak exists in close proximity to last
        // update previous peak
        else if (samples_from_last_peak < ref_seq_len - peak_det_tol)
        {
            // check if this peak is higher than the previous
            if (prev_peak_val < curr_peak_value)
            {
                LOG_DEBUG_FMT("*PeakDet* : Update previous peak. "
                              "Last peak val '%1%' is less than current val '%2%'.",
                              prev_peak_val, curr_peak_value);
                updatePrevPeak();
                insertPeak(corr_sample, curr_peak_value, samp_time);
            }
            // else -> do nothing
        }
        // peak found at the right stop
        // insert as a new peak and wait till its updated to the right spot
        else
        {
            if (prev_peak_val < 0.8 * curr_peak_value)
            {
                LOG_DEBUG("*PeakDet* : Update previous peak.");
                //   "Last peak val '%1%' is less than 80\% of current val '%2%'.",
                //   prev_peak_val, curr_peak_value);
                updatePrevPeak();
                if (peaks_count > 2)
                    LOG_WARN("This should not happen at the in-between peaks! Only first peak might show this artifact! ");
            }
            insertPeak(corr_sample, curr_peak_value, samp_time);
        }
    }
}

void PeakDetectionClass::increase_samples_counter()
{
    if (peaks_count > 0)
    {
        ++samples_from_first_peak;
    }
}

float PeakDetectionClass::avg_of_peak_vals()
{
    float e2e_est_ref_sig_amp = 0.0;
    float max_peak = get_max_peak_val();

    // average channel power from corr_samples
    for (int i = 1; i < total_num_peaks - 1; ++i)
        e2e_est_ref_sig_amp += std::abs(corr_samples[i]);

    e2e_est_ref_sig_amp = e2e_est_ref_sig_amp / (total_num_peaks - 2) / ref_seq_len;

    // update max_pnr
    max_pnr = std::max(max_peak * max_peak_mul, pnr_threshold);
    return e2e_est_ref_sig_amp;
}

uhd::time_spec_t PeakDetectionClass::get_sync_time()
{
    return peak_times[peaks_count - sync_with_peak_from_last];
}

float PeakDetectionClass::estimate_phase_drift()
{
    std::vector<std::complex<float>> peak_corr_vals(corr_samples, corr_samples + peaks_count);
    std::vector<double> phases = unwrap(peak_corr_vals); // returns phases between [-pi, pi]
    // uhd::time_spec_t clock_offset = peak_times[0];
    double init_phase_shift = phases[0];
    double phase_time_prod = 0, time_sqr = 0;
    // linear fit to phase offsets
    for (size_t i = 1; i < peaks_count; ++i)
    {
        // double timer_diff = (peak_times[i] - clock_offset).get_real_secs();
        phase_time_prod += (phases[i] - init_phase_shift) * i;
        time_sqr += i * i;
    }
    double phase_drift_rate = phase_time_prod / time_sqr;
    return phase_drift_rate / ref_seq_len; // radians per symbol
}

int PeakDetectionClass::updatePeaksAfterCFO(const std::vector<float> &abs_corr_vals, const std::deque<uhd::time_spec_t> &new_timer)
{
    // find index of first possible peak
    int first_peak_index = std::floor(ref_seq_len / 2), final_fpi = 0;
    float max_peak_avg = 0.0;
    for (int i = 0; i < ref_seq_len + std::floor(ref_seq_len / 2); ++i)
    {
        float tmp = 0.0;
        for (int j = 0; j < total_num_peaks; ++j)
        {
            size_t c_ind = first_peak_index + i + j * ref_seq_len;
            if (c_ind > abs_corr_vals.size())
                LOG_WARN("PeakDetectionClass::updatePeaksAfterCFO -> Index out of range!");
            tmp += abs_corr_vals[first_peak_index + i + j * ref_seq_len];
        }
        tmp /= total_num_peaks;
        if (tmp > max_peak_avg)
        {
            max_peak_avg = tmp;
            LOG_INFO_FMT("Current Max peak avg est = %1%", max_peak_avg);
            final_fpi = first_peak_index + i;
        }
    }

    for (int i = 0; i < total_num_peaks; ++i)
    {
        peak_vals[i] = abs_corr_vals[final_fpi + i * ref_seq_len] / ref_seq_len / noise_level;
        peak_times[i] = new_timer[final_fpi + i * ref_seq_len];
    }

    int ref_start_index = final_fpi - std::floor(ref_seq_len / 2);

    return ref_start_index;
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