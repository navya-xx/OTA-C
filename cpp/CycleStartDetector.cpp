#include "CycleStartDetector.hpp"
#include "utility_funcs.hpp"

static size_t PEAK_DETECTION_TOLERANCE = 2;
static float MAX_PEAK_MULT_FACTOR = 0.9;
static float MIN_CH_POW_EST = 0.01;

extern const bool DEBUG;

template <typename samp_type>
CycleStartDetector<samp_type>::CycleStartDetector(
    size_t capacity,
    uhd::time_spec_t sample_duration,
    size_t num_samp_corr,
    size_t N_zfc,
    size_t m_zfc,
    size_t R_zfc,
    float init_noise_level,
    float pnr_threshold) : samples_buffer(capacity),
                           save_buffer(N_zfc * (R_zfc + 2), samp_type(0.0, 0.0)),
                           successful_detection(false),
                           timer(capacity),
                           num_samp_corr(num_samp_corr),
                           sample_duration(sample_duration),
                           capacity(capacity),
                           front(0),
                           rear(0),
                           num_produced(0),
                           N_zfc(N_zfc),
                           m_zfc(m_zfc),
                           R_zfc(R_zfc),
                           peaks_count(0),
                           pnr_threshold(pnr_threshold),
                           curr_pnr_threshold(pnr_threshold),
                           peak_indices(R_zfc),
                           peak_vals(R_zfc),
                           peak_times(R_zfc),
                           init_noise_level(init_noise_level),
                           noise_counter(0)
{
    // Initialize zfc_seq
    zfc_seq = generateZadoffChuSequence(N_zfc, m_zfc);
}

template <typename samp_type>
void CycleStartDetector<samp_type>::produce(const std::vector<samp_type> &samples, const size_t &samples_size, const uhd::time_spec_t &time)
{
    boost::unique_lock<boost::mutex> lock(mtx);

    cv_producer.wait(lock, [this, samples_size]
                     { return capacity - num_produced >= samples_size; }); // Wait for enough space to produce

    // insert first timer
    uhd::time_spec_t next_time = time;

    // insert samples into the buffer
    for (const auto &sample : samples)
    {
        samples_buffer[rear] = sample;
        timer[rear] = next_time; // store absolute sample times

        rear = (rear + 1) % capacity;
        next_time += sample_duration;
    }

    num_produced += samples_size;

    // if (DEBUG)
    //     std::cout << "Produced " << samples_size << " samples. Notifying consumer..." << std::endl;

    cv_consumer.notify_one(); // Notify consumer that new data is available
}

template <typename samp_type>
bool CycleStartDetector<samp_type>::consume()
{
    boost::unique_lock<boost::mutex> lock(mtx);

    cv_consumer.wait(lock, [this]
                     { return num_produced >= num_samp_corr * 2; }); // Wait until minimum number of samples are produced

    // if (DEBUG)
    //     std::cout << "Consuming " << num_samp_corr << " samples..." << std::endl;

    bool successful_detection = false;
    auto result = correlation_operation(); // Run the operation function on the samples

    if (result == 0)
    { // No peak found -> discard all samples except last N_zfc for possible partial capture
        front = (front + num_samp_corr) % capacity;
        num_produced -= num_samp_corr;
    }
    else if (result == 1)
    { // found all peaks!
        successful_detection = true;
    }
    else
    {
        throw std::runtime_error("Wrong detection flag!");
    }

    // if (DEBUG)
    //     std::cout << "Consumed " << num_samp_corr << " samples. Notifying Producer..." << std::endl;

    cv_producer.notify_one(); // Notify producer that space is available

    if (successful_detection)
    {
        return true;
    }
    else
    {
        return false;
    }
}

template <typename samp_type>
void CycleStartDetector<samp_type>::save_complex_data_to_file(std::ofstream &outfile)
{
    if (save_buffer_flag == true)
    {
        auto complexDeque = save_buffer;
        if (outfile.is_open()) // we save as csv for analysis in python
        {
            for (auto it = complexDeque.begin(); it != complexDeque.end(); ++it)
            {
                outfile << it->real() << "|" << it->imag();
                if (std::next(it) != complexDeque.end())
                {
                    outfile << ",";
                }
            }
            outfile.close();
        }
    }
}

template <typename samp_type>
int CycleStartDetector<samp_type>::correlation_operation()
{
    bool found_peak = false;
    bool first_peak = true;
    size_t last_peak = 0;
    float last_pnr_val = 0.0;
    size_t adjacent_spacing;
    float noise_level = init_noise_level;

    // Perform cross-correlation
    for (size_t i = 0; i < num_samp_corr; ++i)
    {

        if (peaks_count > 0)
            ++samples_from_first_peak;

        if (peaks_count == R_zfc) // run for another XX times to save rest of the ref signal in save_buffer
        {
            last_peak = peak_indices[peaks_count - 1];
            adjacent_spacing = samples_from_first_peak - last_peak;
            if (adjacent_spacing > 2 * N_zfc)
            {
                if (DEBUG)
                    std::cout << "Breaking! " << adjacent_spacing << " after last peak." << std::endl;
                successful_detection = true;
                break;
            }
        }

        // save values for later processing
        if (save_buffer_flag == true)
        {
            save_buffer.pop_front();
            save_buffer.push_back(samples_buffer[(front + i) % capacity]);
        }

        // compute correlation
        samp_type corr(0.0, 0.0);
        for (size_t j = 0; j < N_zfc; ++j)
        {
            corr += samples_buffer[(front + i + j) % capacity] * std::conj(zfc_seq[j]);
        }

        float abs_val = std::abs(corr) / N_zfc;
        float peak_to_noise_ratio = (abs_val / noise_level);

        if (peak_to_noise_ratio > curr_pnr_threshold)
        {
            found_peak = true;
            if (DEBUG)
                std::cout << "PNR " << peak_to_noise_ratio << ", Threshold " << curr_pnr_threshold << ", at " << i << std::endl;

            if (peaks_count <= R_zfc) // fill in the first peak
            {
                // Detect the first peak correctly. Rest should follow at N_zfc spacing.
                if (first_peak and peaks_count == 0)
                {
                    peak_indices[0] = 0; // set first peak index to zero
                    peak_vals[0] = abs_val;
                    peak_times[0] = timer[(front + i) % capacity];
                    peaks_count = 1;
                    first_peak = false;
                    curr_pnr_threshold = abs_val * MAX_PEAK_MULT_FACTOR / noise_level; // update pnr_threshold
                    if (DEBUG)
                        std::cout << "Peak number " << peaks_count << " found at " << peak_indices[peaks_count - 1] << ". PNR : " << peak_to_noise_ratio << std::endl;
                    continue;
                }
                else // next peaks
                {
                    last_peak = peak_indices[peaks_count - 1];
                    last_pnr_val = peak_vals[peaks_count - 1] / noise_level;
                    // auto max_peak_val_it = std::max_element(peak_vals.begin(), peak_vals.end());
                    // curr_pnr_threshold = (*max_peak_val_it * 0.9 / noise_level);

                    // distance of current peak from last
                    adjacent_spacing = samples_from_first_peak - last_peak;
                    if (DEBUG)
                        std::cout << "Peaks diff : " << adjacent_spacing << std::endl;

                    // next peak is too far from the last
                    if (adjacent_spacing > N_zfc + PEAK_DETECTION_TOLERANCE) // reset to this peak
                    {
                        // reset peaks from the start
                        peak_indices[0] = 0;
                        peak_vals[0] = abs_val;
                        peak_times[0] = timer[(front + i) % capacity];
                        peaks_count = 1;
                        samples_from_first_peak = 0;
                        curr_pnr_threshold = abs_val * MAX_PEAK_MULT_FACTOR / noise_level;
                        if (DEBUG)
                            std::cout << "Reset peaks -> number " << peaks_count << " found at " << peak_indices[peaks_count - 1] << "! PNR : " << peak_to_noise_ratio << std::endl;
                    }
                    else if (adjacent_spacing < N_zfc - PEAK_DETECTION_TOLERANCE) // a peak exists in close proximity to last
                    {
                        if (last_pnr_val < peak_to_noise_ratio) // check is this peak is higher than the previous
                        {
                            // update last peak
                            if (peaks_count == 1) // this is still the first peak
                            {
                                peak_indices[0] = 0;
                                samples_from_first_peak = 0;
                            }
                            else
                            {
                                peak_indices[peaks_count - 1] = adjacent_spacing;
                            }
                            peak_vals[peaks_count - 1] = abs_val;
                            peak_times[peaks_count - 1] = timer[(front + i) % capacity];
                            if (DEBUG)
                                std::cout << "Updated peak number " << peaks_count << " found at " << adjacent_spacing << "! PNR : " << peak_to_noise_ratio << std::endl;
                            auto max_peak_val_it = std::max_element(peak_vals.begin(), peak_vals.end());
                            curr_pnr_threshold = (*max_peak_val_it * MAX_PEAK_MULT_FACTOR / noise_level);
                        } // otherwise ignore this peak and continue
                        else
                        {
                            if (DEBUG)
                                std::cout << "New peak is NOT higher than the previous. Skipping..." << std::endl;
                        }
                    }
                    else // new peak found within tolerance levels
                    {
                        // save peak
                        peak_indices[peaks_count] = adjacent_spacing;
                        peak_vals[peaks_count] = abs_val;
                        peak_times[peaks_count] = timer[(front + i) % capacity];
                        ++peaks_count;
                        if (DEBUG)
                            std::cout << "Peak number " << peaks_count << " found at " << adjacent_spacing << "! PNR : " << peak_to_noise_ratio << std::endl;
                        auto max_peak_val_it = std::max_element(peak_vals.begin(), peak_vals.end());
                        curr_pnr_threshold = (*max_peak_val_it * MAX_PEAK_MULT_FACTOR / noise_level);
                    }
                }
            }
            else
            {
                throw std::runtime_error("Another peak detected after last! This should not happen...");
                // std::cout << "Another peak detected after last! This should not happen..." << std::endl;
            }
        }
        else // use abs_corr for updating the noise_level
        {
            if (std::abs(abs_val - noise_level) / noise_level < 0.1)
            {
                noise_level = (noise_counter * noise_level + abs_val) / (noise_counter + 1);
                ++noise_counter;
            }
        }
    }

    init_noise_level = noise_level;

    if (successful_detection)
    {
        if (DEBUG)
            std::cout << "All peaks are successfully detected! Total peaks " << R_zfc << std::endl;
        // send last peak
        return 1;
    }
    // else if (peaks_count < R_zfc) // not all peaks are detected yet
    // {
    //     std::cout << "All peaks are not detected! We detected " << peaks_count
    //               << " peaks out of a total of " << R_zfc << std::endl;
    //     return {-1, peak_indices.front()};
    // }
    else if (not found_peak)
    {
        // reset counters
        peaks_count = 0;
        return 0;
    }
    else
    {
        return 0;
    }
}

// get important private values
template <typename samp_type>
std::vector<size_t> CycleStartDetector<samp_type>::get_peakindices()
{
    return peak_indices;
}

template <typename samp_type>
std::vector<uhd::time_spec_t> CycleStartDetector<samp_type>::get_timestamps()
{
    return peak_times;
}

template <typename samp_type>
std::vector<float> CycleStartDetector<samp_type>::get_peakvals()
{
    return peak_vals;
}

template <typename samp_type>
size_t CycleStartDetector<samp_type>::get_peakscount()
{
    return peaks_count;
}

template class CycleStartDetector<std::complex<float>>;