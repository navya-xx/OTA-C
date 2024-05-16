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
    total_num_peaks = parser.getValue_int("Ref-R-zfc");

    ref_seq_len = parser.getValue_int("Ref-N-zfc");
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

    if (save_buffer_flag)
    {
        save_buffer.resize(ref_seq_len * total_num_peaks * 2, std::complex<float>(0.0, 0.0));
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

void PeakDetectionClass::updatePrevPeak(const float &peak_val, const uhd::time_spec_t &peak_time)
{
    if (peaks_count == 0)
        std::cerr << "ERROR: Cannot update! No peaks found." << std::endl;
    // update last peak
    else if (peaks_count == 1) // this is still the first peak
    {
        resetPeaks();
    }
    else
    {
        removeLastPeak();
    }

    insertPeak(peak_val, peak_time);
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
    float max_peak_val = get_max_peak_val();
    curr_pnr_threshold = std::max(max_peak_mul * max_peak_val / noise_level, pnr_threshold);
}

void PeakDetectionClass::resetPeaks()
{
    peaks_count = 0;
    // curr_pnr_threshold = pnr_threshold;
    // noise_level = init_noise_level;
    samples_from_first_peak = 0;
    detection_flag = false;
}

void PeakDetectionClass::updateNoiseLevel(const float &avg_ampl, const size_t &num_samps)
{
    // only tolerate max 10% change in noise level
    // if (DEBUG)
    //     std::cout << "Update noise :  current val = " << std::abs(avg_ampl) << ", curr noise lev = " << noise_level << std::endl;

    if (std::abs(avg_ampl - noise_level) / noise_level < 0.1)
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

void PeakDetectionClass::save_into_buffer(const std::complex<float> &sample)
{
    if (save_buffer_flag)
    {
        save_buffer.pop_front();
        save_buffer.push_back(sample);
    }
}

void PeakDetectionClass::save_complex_data_to_file(const std::string &file)
{
    if (save_buffer_flag)
    {
        if (DEBUG)
            std::cout << "Saving data to file " << file << std::endl;

        std::ofstream outfile;
        outfile.open(file);

        if (outfile.is_open()) // we save as csv for analysis in python
        {
            for (auto it = save_buffer.begin(); it != save_buffer.end(); ++it)
            {
                outfile << it->real() << "|" << it->imag();
                if (std::next(it) != save_buffer.end())
                {
                    outfile << ",";
                }
            }
            outfile << std::endl;
            outfile.close();
        }
        else
        {
            std::cerr << "ERROR: File not open!" << std::endl;
        }
    }
}

bool PeakDetectionClass::next()
{
    if (peaks_count > 0)
    {
        int adjacent_spacing = samples_from_first_peak - prev_peak_index;
        ++samples_from_first_peak;

        // run for another (2 * ref_seq_len) times to save extra symbols in save_buffer
        if (adjacent_spacing > 2 * ref_seq_len)
        {
            if (peaks_count == total_num_peaks)
            {
                if (DEBUG)
                    std::cout << "Successful Detection! Processed " << adjacent_spacing << " samples without detecting peak." << std::endl;

                detection_flag = true;
            }
            else // reset peaks
            {
                if (DEBUG)
                {
                    if (peaks_count > total_num_peaks)
                        std::cerr << "\t\t -> More peaks than expected! <- PeakDetectionClass" << std::endl;
                    else
                        std::cout << "\t\t -> Less peaks detected! <- PeakDetectionClass" << std::endl;
                }
                detection_flag = false;
                resetPeaks();
            }
            return false; // iterations stop here
        }
        else
            return true; // else continue
    }

    else
        return true;
}

bool PeakDetectionClass::process_corr(const float &abs_corr_val, const uhd::time_spec_t &samp_time)
{
    float abs_corr_to_noise_ratio = abs_corr_val / noise_level;

    // if (peak_to_noise_ratio > 1.0)
    //     std::cout << "PNR: " << abs_corr_to_noise_ratio << ", noise level: " << noise_level << std::endl;

    if (abs_corr_to_noise_ratio > curr_pnr_threshold)
    {
        if (DEBUG)
        {
            std::cout << std::endl;
            std::cout << "PNR = " << abs_corr_val << "/" << noise_level << " = " << abs_corr_to_noise_ratio << " > " << curr_pnr_threshold << std::endl;
        }

        // First peak
        if (peaks_count == 0)
        {
            insertPeak(abs_corr_val, samp_time);
        }
        else // next peaks
        {
            // distance of current peak from last
            int adjacent_spacing = samples_from_first_peak - prev_peak_index;

            // next peak is too far from the last
            if (adjacent_spacing > ref_seq_len + peak_det_tol) // false peak -> reset
            {

                resetPeaks();
                insertPeak(abs_corr_val, samp_time);
            }
            else if (adjacent_spacing < ref_seq_len - peak_det_tol) // a peak exists in close proximity to last
            {

                if (prev_peak_val < abs_corr_val) // check if this peak is higher than the previous
                {
                    updatePrevPeak(abs_corr_val, samp_time);

                } // otherwise ignore this peak and continue
            }
            else // new peak found within tolerance levels
            {
                insertPeak(abs_corr_val, samp_time);
            }
        }
        return true; // a peak is found
    }
    else
    {
        // updateNoiseLevel(abs_val);
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