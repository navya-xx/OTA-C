#include "PeakDetection.hpp"
extern const bool DEBUG;

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

    peak_det_tol = parser.getValue_int("peak-det-tol");
    max_peak_mul = parser.getValue_float("max-peak-mul");
    sync_with_peak_from_last = parser.getValue_int("sync-with-peak-from-last");

    peak_indices = new size_t[total_num_peaks];
    peak_vals = new float[total_num_peaks];
    peak_times = new uhd::time_spec_t[total_num_peaks];

    ref_signal.resize(ref_seq_len * (total_num_peaks + 1));

    prev_peak_index = 0;
    peaks_count = 0;
    noise_counter = 0;
    noise_level = init_noise_level;

    save_buffer_flag = parser.is_save_buffer();

    // whether to save complex samples or floats (see CycleStartDetector::correlation_operation() )
    is_save_buffer_complex = true;

    if (save_buffer_flag)
    {
        if (is_save_buffer_complex)
            save_buffer_complex.resize(ref_seq_len * total_num_peaks * 2, std::complex<float>(0.0, 0.0));
        else
            save_buffer_float.resize(ref_seq_len * total_num_peaks * 2, float(0.0));
    }
};

float *PeakDetectionClass::get_peak_vals()
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
        std::cout << "Peak " << i + 1 << " channel power = " << peak_vals[i] << std::endl;
        if (i == num_peaks_detected - 1)
            continue;
        else
        {
            std::cout << "Peak " << i + 2 << " and " << i + 1;
            std::cout << " : Index diff = " << peak_indices[i + 1] - peak_indices[i];
            std::cout << ", Time diff = " << (peak_times[i + 1] - peak_times[i]).get_real_secs() * 1e6 << " microsecs" << std::endl;
        }
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
    // float max_peak_val = get_max_peak_val();
    if (max_pnr > 0.0)
        curr_pnr_threshold = std::min(std::max(max_peak_mul * prev_peak_val / noise_level, pnr_threshold), max_pnr);
    else
        curr_pnr_threshold = std::min(std::max(max_peak_mul * prev_peak_val / noise_level, pnr_threshold), float(50.0));
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

    // max_pnr = 0;

    delete[] peak_indices;
    delete[] peak_vals;
    delete[] peak_times;
    peak_indices = new size_t[total_num_peaks];
    peak_vals = new float[total_num_peaks];
    peak_times = new uhd::time_spec_t[total_num_peaks];

    // ref_signal.clear();
    // ref_signal.resize(ref_seq_len * (total_num_peaks + 1));
}

void PeakDetectionClass::reset_peaks_counter()
{
    peaks_count = 0;
    samples_from_first_peak = 0;
    prev_peak_index = 0;

    // curr_pnr_threshold = pnr_threshold;
    // noise_level = init_noise_level;

    std::cout << "\t\t -> Reset peaks counter" << std::endl;
}

void PeakDetectionClass::insertPeak(const float &peak_val, const uhd::time_spec_t &peak_time)
{
    // when more than 2 peaks, check previous registered peaks for correctness
    if (peaks_count > 1)
    {
        size_t reg_peaks_spacing = peak_indices[peaks_count - 1] - peak_indices[peaks_count - 2];
        if (DEBUG)
            std::cout << "\t\t -> Peaks diff " << reg_peaks_spacing << std::endl;
        // if spacing is not as expected, cancel all previous peaks except the last registered peak
        if (reg_peaks_spacing > ref_seq_len + 1 or reg_peaks_spacing < ref_seq_len - 1)
        {
            if (DEBUG)
                std::cout << "\t\t -> Insert - Remove all except last peak" << std::endl;
            peak_indices[0] = 0;
            peak_vals[0] = peak_vals[peaks_count - 1];
            peak_times[0] = peak_times[peaks_count - 1];
            samples_from_first_peak = samples_from_first_peak - peak_indices[peaks_count - 1];
            prev_peak_index = 0;
            peaks_count = 1;
        }
        else
        {
            if (DEBUG)
                std::cout << "\t\t -> Insert - Success" << std::endl;
        }
    }

    // check the last peak -> if at correct spot, return success
    if (peaks_count == total_num_peaks)
    {
        size_t last_peak_spacing = samples_from_first_peak - peak_indices[peaks_count - 1];
        if (last_peak_spacing > ref_seq_len - 1 and last_peak_spacing < ref_seq_len + 1)
        {
            // all total_num_peaks are clear
            detection_flag = true;
            if (DEBUG)
                std::cout << "\t\t -> Successful detection" << std::endl;
            print_peaks_data();
            return;
        }
        else
        {
            if (DEBUG)
                std::cout << "\t\t -> Last peak is not at right spot yet. Wait for more data." << std::endl;
        }
    }
    // if we reach here, that means all total_num_peaks are at correct place.
    else if (peaks_count == total_num_peaks + 1)
    {
        // all total_num_peaks are clear
        detection_flag = true;
        if (DEBUG)
            std::cout << "\t\t -> Successful detection" << std::endl;
        print_peaks_data();
        return;
    }

    if (peaks_count == 0) // First peak starts with index 0
    {
        samples_from_first_peak = 0;
        if (DEBUG)
            std::cout << "\t\t -> first peak" << std::endl;
    }

    peak_indices[peaks_count] = samples_from_first_peak;
    peak_vals[peaks_count] = peak_val;
    peak_times[peaks_count] = peak_time;

    update_pnr_threshold();

    if (DEBUG)
        std::cout << currentDateTime() << "***Inserted new Peak, count = " << peaks_count << ", PNR = " << peak_val / noise_level << ", threshold = " << curr_pnr_threshold << ", samples from last peak = " << samples_from_first_peak - prev_peak_index << std::endl;

    ++peaks_count;
    prev_peak_index = samples_from_first_peak;
    prev_peak_val = peak_val;
}

void PeakDetectionClass::removeLastPeak()
{
    // decrement counter
    --peaks_count;
}

void PeakDetectionClass::updatePrevPeak()
{
    if (peaks_count == 0)
        std::cerr << "ERROR: Cannot update! No peaks found." << std::endl;
    // update last peak
    else
    {
        removeLastPeak();
        if (DEBUG)
            std::cout << "\t\t -> Update peak" << std::endl;
    }
}

bool PeakDetectionClass::process_corr(const float &abs_corr_val, const uhd::time_spec_t &samp_time)
{

    if ((abs_corr_val / noise_level) > curr_pnr_threshold)
    {
        // First peak
        if (peaks_count == 0)
        {
            insertPeak(abs_corr_val, samp_time);
            // if (DEBUG)
            //     std::cout << "\t\t -> PNR = " << abs_corr_val << "/" << noise_level << " = " << abs_corr_to_noise_ratio << " > " << curr_pnr_threshold << std::endl;
        }
        else
        {
            // distance of current peak from last
            if (prev_peak_index > samples_from_first_peak)
                std::cerr << "Prev peak index > samples from first peak = " << prev_peak_index << " >" << samples_from_first_peak << ", at peaks count = " << peaks_count << std::endl;

            size_t samples_from_last_peak = samples_from_first_peak - prev_peak_index;

            // next peak is too far from the last
            // reset peaks and mark this peak as first
            if (samples_from_last_peak > ref_seq_len + 5)
            {
                if (DEBUG)
                    std::cout << "\t\t Resetting -- samples from last peak = " << samples_from_last_peak << std::endl;
                reset_peaks_counter();
                insertPeak(abs_corr_val, samp_time);
            }
            // a higher peak exists in close proximity to last
            // update previous peak
            else if (samples_from_last_peak < ref_seq_len - 5)
            {
                // check if this peak is higher than the previous
                if (prev_peak_val < abs_corr_val)
                {
                    updatePrevPeak();
                    insertPeak(abs_corr_val, samp_time);
                }
                // else -> do nothing
            }
            // insert as a new peak and wait till its updated to the right spot
            else
                insertPeak(abs_corr_val, samp_time);

            // if (DEBUG)
            // {
            //     std::cout << "\t\t -> PNR = " << abs_corr_val << "/" << noise_level << " = " << abs_corr_to_noise_ratio << " > " << curr_pnr_threshold << std::flush;
            //     std::cout << "\t spacing " << samples_from_last_peak << std::endl;
            // }
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
    if (total_num_peaks > 1)
    {
        // average channel power from peak_vals
        for (int i = 0; i < total_num_peaks; ++i)
            e2e_est_ref_sig_amp += peak_vals[i];

        e2e_est_ref_sig_amp = e2e_est_ref_sig_amp / total_num_peaks;
    }
    else
        e2e_est_ref_sig_amp = peak_vals[0];

    // update max_pnr
    max_pnr = std::max(max_peak / noise_level * max_peak_mul, pnr_threshold);
    return e2e_est_ref_sig_amp;
}

float PeakDetectionClass::est_ch_pow_from_capture_ref_sig()
{
    float sig_ampl = 0.0;
    size_t counter = 0;

    for (auto &ref_symb : ref_signal)
    {
        float elem = std::abs(ref_symb);
        if (elem > 0)
        {
            sig_ampl += elem;
            ++counter;
        }
    }

    return sig_ampl / counter;
}

uhd::time_spec_t PeakDetectionClass::get_sync_time()
{
    return peak_times[peaks_count - sync_with_peak_from_last];
}

void PeakDetectionClass::updateNoiseLevel(const float &avg_ampl, const size_t &num_samps)
{
    // only tolerate max 10% change in noise level
    // if (DEBUG)
    //     std::cout << "Update noise :  current val = " << std::abs(avg_ampl) << ", curr noise lev = " << noise_level << std::endl;

    if (std::abs(avg_ampl - noise_level) / noise_level < 1.0)
    {
        // update noise level by iteratively averaging
        noise_level = (noise_counter * noise_level + avg_ampl * num_samps) / (noise_counter + num_samps);

        if (DEBUG)
        {
            std::cout << '\r';
            std::cout << "New noise level = " << noise_level;
            std::cout.flush();
        }

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
            std::cerr << "Incorrect peak index : peak " << i << " at " << peak_indices[i] << " and  peak " << i + 1 << " at " << peak_indices[i + 1] << std::endl;
            return false;
        }
    }

    return true;
}

bool PeakDetectionClass::next()
{
    // No peak -> continue to next
    if (peaks_count == 0)
        return true;
    // Not enough peaks -> continue
    else if (peaks_count < total_num_peaks)
    {
        ++samples_from_first_peak;
        return true;
    }
    // Required number of peaks found -> check!
    else if (peaks_count == total_num_peaks)
    {
        // check if peak detection successful or not

        if (check_peaks())
        {
            std::cout << "Check peaks -> Success! Peaks Detection Successful! Finishing..." << std::endl;
            detection_flag = true;
            print_peaks_data();
            return false;
        }
        else
        {
            ++samples_from_first_peak;
            return true;
        }
    }
    else
    {
        std::cout << "More than " << total_num_peaks << " peaks captured. Resetting peaks." << std::endl;
        reset_peaks_counter();
        return true;
    }
}

void PeakDetectionClass::save_float_data_into_buffer(const float &sample)
{
    if (save_buffer_flag)
    {
        save_buffer_float.pop_front();
        save_buffer_float.push_back(sample);
    }
}

void PeakDetectionClass::save_complex_data_into_buffer(const std::complex<float> &sample)
{
    ref_signal.pop_front();
    ref_signal.push_back(sample);

    if (save_buffer_flag)
    {
        save_buffer_complex.pop_front();
        save_buffer_complex.push_back(sample);
    }
}

void PeakDetectionClass::save_data_to_file(const std::string &file)
{
    if (save_buffer_flag)
    {
        if (is_save_buffer_complex)
        {
            std::vector<std::complex<float>> vector_buffer_complex(save_buffer_complex.begin(), save_buffer_complex.end());
            save_complex_data_to_file(file, vector_buffer_complex);
        }
        else
        {
            std::vector<float> vector_buffer_float(save_buffer_float.begin(), save_buffer_float.end());
            save_float_data_to_file(file, vector_buffer_float);
        }
    }
}

// PeakDetectionClass::~PeakDetectionClass()
// {
//     delete[] peak_indices;
//     delete[] peak_vals;
//     delete[] peak_times;

//     if (save_buffer_flag)
//         save_buffer.clear();
// }