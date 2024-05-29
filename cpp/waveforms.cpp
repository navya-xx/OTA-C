#include "waveforms.hpp"

/* Class object for generating different waveforms */

// Function to generate Zadoff-Chu sequence
std::vector<std::complex<float>> WaveformGenerator::generateZadoffChuSequence(size_t wf_len, size_t zfc_q, float scale)
{
    std::vector<std::complex<float>> sequence(wf_len);

    // Calculate sequence
    for (size_t n = 0; n < wf_len; ++n)
    {
        float phase = -M_PI * zfc_q * n * (n + 1) / wf_len;
        sequence[n] = scale * std::exp(std::complex<float>(0, phase));
    }

    return sequence;
}

// Function to generate a vector of complex random variables on the unit circle
std::vector<std::complex<float>> WaveformGenerator::generateUnitCircleRandom(size_t rand_seed, size_t wf_len, float scale)
{
    // Seed for random number generation
    std::mt19937 generator(rand_seed);

    // std::random_device rd;
    std::uniform_real_distribution<> distribution(0.0, 2 * M_PI); // Uniform distribution for phase

    // Vector to store complex numbers
    std::vector<std::complex<float>> sequence;

    // Generate random phases and construct complex numbers
    for (int i = 0; i < wf_len; ++i)
    {
        float phase = distribution(generator);
        sequence.emplace_back(std::polar(scale, phase)); // Construct complex number with unit magnitude and phase
    }

    return sequence;
}

std::vector<std::complex<float>> WaveformGenerator::generateImpulse(size_t wf_len, float scale)
{
    std::vector<std::complex<float>> sequence(wf_len);

    float phase = 0.71; // almost 45°
    size_t impulse_loc = wf_len / 2;

    // middle element is set to amplitude=scale for impulse
    sequence[impulse_loc - 1] = std::polar(scale, phase);

    return sequence;
}

std::vector<std::complex<float>> WaveformGenerator::generate_waveform(WAVEFORM_TYPE wf_type, size_t wf_len, size_t wf_reps, size_t wf_gap, size_t zfc_q, float scale, size_t rand_seed, bool is_cyclic_padding)
{

    std::vector<std::complex<float>> final_sequence;
    std::vector<std::complex<float>> sequence;

    switch (wf_type)
    {
    case WAVEFORM_TYPE::ZFC:
        sequence = generateZadoffChuSequence(wf_len, zfc_q, scale);
        // std::cout << "\t\t generating ZFC seq" << std::endl;
        break;

    case WAVEFORM_TYPE::UNIT_RAND:
        sequence = generateUnitCircleRandom(rand_seed, wf_len, scale);
        // std::cout << "\t\t generating RANDOM seq" << std::endl;
        break;

    case WAVEFORM_TYPE::IMPULSE:
        sequence = generateImpulse(wf_len, scale);
        // std::cout << "\t\t generating IMPULSE seq" << std::endl;
        break;

    default:
        break;
    }

    // add reps
    for (size_t i = 0; i < wf_reps; ++i)
    {
        final_sequence.insert(final_sequence.end(), sequence.begin(), sequence.end());
        if ((wf_reps > 1) and (i < wf_reps - 1) and (wf_gap > 0))
            final_sequence.insert(final_sequence.end(), wf_gap, std::complex<float>(0.0, 0.0));
    }

    // add cyclic_padding
    if (is_cyclic_padding)
    {
        size_t cyclic_shift = 2;
        std::vector<std::complex<float>> scaled_seq;
        float scaleFactor = 0.3;
        for (auto &element : sequence)
        {
            scaled_seq.insert(scaled_seq.end(), element * scaleFactor);
        }
        final_sequence.insert(final_sequence.begin(), scaled_seq.begin() + cyclic_shift, scaled_seq.end());
        final_sequence.insert(final_sequence.end(), scaled_seq.begin(), scaled_seq.end() - cyclic_shift);
    }

    return final_sequence;
}