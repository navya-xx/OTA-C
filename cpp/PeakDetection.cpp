#include "PeakDetection.hpp"
extern const bool DEBUG;

PeakDetectionClass::PeakDetectionClass(
    ConfigParser &parser,
    const float &init_noise_level,
    bool save_buffer_flag) : parser(parser),
                             save_buffer_flag(save_buffer_flag),
                             detection_flag(false),
                             init_noise_level(init_noise_level)
{

    ref_seq_len = parser.getValue_int("Ref-N-zfc");
    total_num_peaks = parser.getValue_int("Ref-R-zfc");
    pnr_threshold = parser.getValue_float("pnr-threshold");
    curr_pnr_threshold = pnr_threshold;

    peak_det_tol = parser.getValue_int("peak-det-tol");
    max_peak_mul = parser.getValue_float("max-peak-mul");
    sync_with_peak_from_last = parser.getValue_int("sync-with-peak-from-last");

    peak_indices = new size_t[total_num_peaks];
    peak_vals = new float[total_num_peaks];
    peak_times = new uhd::time_spec_t[total_num_peaks];

    peaks_count = 0;
    noise_counter = 0;
    noise_level = init_noise_level;

    is_save_buffer_complex = false;

    if (save_buffer_flag)
    {
        if (is_save_buffer_complex)
            save_buffer_complex.resize(ref_seq_len * total_num_peaks * 10, float(0.0));
        else
            save_buffer_float.resize(ref_seq_len * total_num_peaks * 10, float(0.0));
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

void PeakDetectionClass::insertPeak(const float &peak_val, const uhd::time_spec_t &peak_time)
{
    if (peaks_count == 0) // First peak starts with index 0
        samples_from_first_peak = 0;

    peak_indices[peaks_count] = size_t(samples_from_first_peak);
    peak_vals[peaks_count] = peak_val;
    peak_times[peaks_count] = peak_time;
    ++peaks_count;
    prev_peak_index = samples_from_first_peak;
    prev_peak_val = peak_val;

    update_pnr_threshold();

    if (DEBUG)
        std::cout << "***Peak " << peaks_count << " -- PNR " << peak_val / noise_level << ", threshold " << curr_pnr_threshold << std::endl
                  << std::endl;
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
    curr_pnr_threshold = std::max(std::max(max_peak_mul * prev_peak_val / noise_level, pnr_threshold), curr_pnr_threshold);
}

void PeakDetectionClass::resetPeaks()
{
    peaks_count = 0;
    samples_from_first_peak = 0;
    curr_pnr_threshold = pnr_threshold;
    detection_flag = false;
    // noise_level = init_noise_level;

    // delete[] peak_indices;
    // delete[] peak_vals;
    // delete[] peak_times;
    // peak_indices = new size_t[total_num_peaks];
    // peak_vals = new float[total_num_peaks];
    // peak_times = new uhd::time_spec_t[total_num_peaks];
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
        if (DEBUG)
            std::cout << "Saving data to file " << file << std::endl;

        std::ofstream outfile(file, std::ios::out | std::ios::binary);

        // Check if the file was opened successfully
        if (!outfile.is_open())
        {
            std::cerr << "Error: Could not open file for writing." << std::endl;
            return;
        }

        if (is_save_buffer_complex)
        { // Write the size of the deque to the file
            size_t size = save_buffer_complex.size();
            outfile.write(reinterpret_cast<char *>(&size), sizeof(size));

            // Write each complex number (real and imaginary parts)
            for (const auto &complex_value : save_buffer_complex)
            {
                float real_val = complex_value.real();
                float complex_val = complex_value.imag();
                outfile.write(reinterpret_cast<char *>(&real_val), sizeof(complex_value.real()));
                outfile.write(reinterpret_cast<char *>(&complex_val), sizeof(complex_value.imag()));
            }
        }
        else
        {
            size_t size = save_buffer_float.size();
            outfile.write(reinterpret_cast<char *>(&size), sizeof(size));

            // Write each complex number (real and imaginary parts)
            for (float &float_value : save_buffer_float)
            {
                outfile.write(reinterpret_cast<char *>(&float_value), sizeof(float_value));
            }
        }

        outfile.close();
        std::cout << "Data saved successfully to " << file << "." << std::endl;
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
        int adjacent_spacing = samples_from_first_peak - prev_peak_index;

        if (check_peaks())
        {
            std::cout << "Check peaks -> Success! Peaks Detection Successful! Finishing..." << std::endl;
            detection_flag = true;
            print_peaks_data();
            return false;
        }
        else
        {
            // wait for till end of window
            // if check still fails -> reset peaks and start again
            if (adjacent_spacing > ref_seq_len - peak_det_tol)
            {
                std::cerr << "Detection unsuccessful. Resetting peaks." << std::endl;
                resetPeaks();
                return true;
            }
            else
            {
                std::cout << "\t\t -> CheckPeak unsuccessful! Waiting for another " << (ref_seq_len - peak_det_tol - adjacent_spacing) << " samples." << std::endl;

                ++samples_from_first_peak;
                return true;
            }
        }
    }
    else
    {
        std::cerr << "!!! ERROR: Should not reach here!!! More than " << total_num_peaks << " peaks captured. Resetting peaks." << std::endl;
        resetPeaks();
        return true;
    }
}

bool PeakDetectionClass::process_corr(const float &abs_corr_val, const uhd::time_spec_t &samp_time)
{
    float abs_corr_to_noise_ratio = abs_corr_val / noise_level;

    if (abs_corr_to_noise_ratio > curr_pnr_threshold)
    {
        // First peak
        if (peaks_count == 0)
        {
            insertPeak(abs_corr_val, samp_time);
            if (DEBUG)
                std::cout << "\t\t -> PNR = " << abs_corr_val << "/" << noise_level << " = " << abs_corr_to_noise_ratio << " > " << curr_pnr_threshold << std::endl;
        }
        else // next peaks
        {
            // distance of current peak from last
            int adjacent_spacing = samples_from_first_peak - prev_peak_index;

            // next peak is too far from the last
            // reset peaks and mark this peak as first
            if (adjacent_spacing > ref_seq_len + peak_det_tol)
            {
                resetPeaks();
                insertPeak(abs_corr_val, samp_time);
            }
            // a higher peak exists in close proximity to last
            // update previous peak
            else if (adjacent_spacing < ref_seq_len - peak_det_tol)
            {
                // check if this peak is higher than the previous
                if (prev_peak_val < abs_corr_val)
                {
                    updatePrevPeak();
                    insertPeak(abs_corr_val, samp_time);
                }
                // else -> do nothing
            }
            // a new peak found within a tolerance from right spot
            // insert as a new peak
            else
                insertPeak(abs_corr_val, samp_time);

            if (DEBUG)
            {
                std::cout << "\t\t -> PNR = " << abs_corr_val << "/" << noise_level << " = " << abs_corr_to_noise_ratio << " > " << curr_pnr_threshold << std::flush;
                std::cout << "\t spacing " << adjacent_spacing << std::endl;
            }
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

float PeakDetectionClass::get_avg_ch_pow()
{
    float ch_pow = 0.0;
    if (peaks_count > 1)
    {
        int num_peaks_detected = peaks_count - 1;

        // ignore last peak for channel power estimation
        for (int i = 0; i < peaks_count - 1; ++i)
        {
            ch_pow += peak_vals[i];
        }

        ch_pow = ch_pow / num_peaks_detected;
    }
    else
        ch_pow = peak_vals[0];

    return ch_pow;
}

uhd::time_spec_t PeakDetectionClass::get_sync_time()
{
    return peak_times[peaks_count - sync_with_peak_from_last];
}

// PeakDetectionClass::~PeakDetectionClass()
// {
//     delete[] peak_indices;
//     delete[] peak_vals;
//     delete[] peak_times;

//     if (save_buffer_flag)
//         save_buffer.clear();
// }