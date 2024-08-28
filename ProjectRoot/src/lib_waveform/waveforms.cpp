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
std::vector<samp_type> WaveformGenerator::generateZadoffChuSequence()
{
    std::vector<samp_type> sequence(wf_len);

    // Calculate sequence
    for (size_t n = 0; n < wf_len; ++n)
    {
        float phase = -M_PI * zfc_q * n * (n + 1) / wf_len;
        sequence[n] = scale * std::exp(samp_type(0, phase));
    }

    return sequence;
}

// Function to generate a vector of complex random variables on the unit circle
std::vector<samp_type> WaveformGenerator::generateUnitCircleRandom()
{
    // Seed for random number generation
    std::mt19937 generator(rand_seed);

    // std::random_device rd;
    std::uniform_real_distribution<> distribution(0.0, 2 * M_PI); // Uniform distribution for phase

    // Vector to store complex numbers
    std::vector<samp_type> sequence;

    // Generate random phases and construct complex numbers
    for (int i = 0; i < wf_len; ++i)
    {
        float phase = distribution(generator);
        sequence.emplace_back(std::polar(scale, phase)); // Construct complex number with unit magnitude and phase
    }

    return sequence;
}

std::vector<samp_type> WaveformGenerator::generateImpulseSignal()
{
    std::vector<samp_type> sequence(wf_len);

    float phase = 0.71; // almost 45°
    size_t impulse_loc = wf_len / 2;

    // middle element is set to amplitude=scale for impulse
    sequence[impulse_loc - 1] = std::polar(scale, phase);

    return sequence;
}

std::vector<samp_type> WaveformGenerator::generateDFTseq()
{
    std::vector<samp_type> sequence(wf_len);
    float scale_down = scale / sqrt(wf_len);

    for (int n = 0; n < wf_len; ++n)
    {
        float angle = 2 * M_PI * zfc_q * n / wf_len;
        sequence[n] = std::polar(scale_down, angle);
    }

    return sequence;
}

std::vector<samp_type> WaveformGenerator::generate_waveform()
{

    std::vector<samp_type> final_sequence;
    std::vector<samp_type> sequence;

    switch (wf_type)
    {
    case WAVEFORM_TYPE::ZFC:
        sequence = generateZadoffChuSequence();
        break;

    case WAVEFORM_TYPE::UNIT_RAND:
        sequence = generateUnitCircleRandom();
        break;

    case WAVEFORM_TYPE::IMPULSE:
        sequence = generateImpulseSignal();
        break;

    case WAVEFORM_TYPE::DFT:
        sequence = generateImpulseSignal();
        break;

    default:
        break;
    }

    // add reps
    for (size_t i = 0; i < wf_reps; ++i)
    {
        final_sequence.insert(final_sequence.end(), sequence.begin(), sequence.end());
        if ((wf_reps > 1) and (i < wf_reps - 1) and (wf_gap > 0))
            final_sequence.insert(final_sequence.end(), wf_gap, samp_type(0.0, 0.0));
    }

    // add zero at the beginning and end
    if (wf_pad > 0)
    {
        final_sequence.insert(final_sequence.begin(), wf_pad, samp_type(0.0, 0.0));
        final_sequence.insert(final_sequence.end(), wf_pad, samp_type(0.0, 0.0));
    }

    return final_sequence;
}