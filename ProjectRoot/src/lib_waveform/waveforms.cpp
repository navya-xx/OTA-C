#include "waveforms.hpp"

/* Class object for generating different waveforms */

// initialize
WaveformGenerator::WaveformGenerator() {};

void WaveformGenerator::initialize(WAVEFORM_TYPE init_wf_type, size_t init_wf_len, size_t init_wf_reps, size_t init_wf_gap, size_t init_wf_pad, size_t init_zfc_q, float init_scale, size_t init_rand_seed)
{
    wf_type = init_wf_type;
    wf_len = init_wf_len;
    wf_reps = init_wf_reps;
    wf_gap = init_wf_gap;
    wf_pad = init_wf_pad;
    zfc_q = init_zfc_q;
    scale = init_scale;
    rand_seed = init_rand_seed;
};

// Function to generate Zadoff-Chu sequence
std::vector<std::complex<float>> WaveformGenerator::generateZadoffChuSequence()
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
std::vector<std::complex<float>> WaveformGenerator::generateUnitCircleRandom()
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

std::vector<std::complex<float>> WaveformGenerator::generateImpulseSignal()
{
    std::vector<std::complex<float>> sequence(wf_len);

    float phase = 0.71; // almost 45°
    size_t impulse_loc = wf_len / 2;

    // middle element is set to amplitude=scale for impulse
    sequence[impulse_loc - 1] = std::polar(scale, phase);

    return sequence;
}

std::vector<std::complex<float>> WaveformGenerator::generate_waveform()
{

    std::vector<std::complex<float>> final_sequence;
    std::vector<std::complex<float>> sequence;

    switch (wf_type)
    {
    case WAVEFORM_TYPE::ZFC:
        sequence = generateZadoffChuSequence();
        // std::cout << "\t\t generating ZFC seq" << std::endl;
        break;

    case WAVEFORM_TYPE::UNIT_RAND:
        sequence = generateUnitCircleRandom();
        // std::cout << "\t\t generating RANDOM seq" << std::endl;
        break;

    case WAVEFORM_TYPE::IMPULSE:
        sequence = generateImpulseSignal();
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

    if (wf_type == WAVEFORM_TYPE::ZFC)
    {
        // add ZFC seq N-1 samples
        final_sequence.insert(final_sequence.begin(), sequence.begin() + 1, sequence.end());
        final_sequence.insert(final_sequence.end(), sequence.begin(), sequence.end() - 1);
    }

    // add zero at the beginning and end
    if (wf_pad > 0)
    {
        final_sequence.insert(final_sequence.begin(), wf_pad, std::complex<float>(0.0, 0.0));
        final_sequence.insert(final_sequence.end(), wf_pad, std::complex<float>(0.0, 0.0));
    }

    return final_sequence;
}