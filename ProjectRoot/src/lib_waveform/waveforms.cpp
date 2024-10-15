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
    std::vector<std::complex<float>> sequence(wf_len);

    // Generate random phases and construct complex numbers
    for (int i = 0; i < wf_len; ++i)
    {
        float phase = distribution(generator);
        sequence[i] = std::polar(scale, phase); // Construct complex number with unit magnitude and phase
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

std::vector<std::complex<float>> WaveformGenerator::generateDFTseq()
{
    std::vector<std::complex<float>> sequence(wf_len);
    float scale_down = scale / sqrt(wf_len);

    for (int n = 0; n < wf_len; ++n)
    {
        float angle = 2 * M_PI * zfc_q * n / wf_len;
        sequence[n] = std::polar(scale_down, angle);
    }

    return sequence;
}

std::vector<std::complex<float>> WaveformGenerator::generateQPSKSymbols(const std::vector<int> &binarySequence)
{
    // Output vector for QPSK symbols
    std::vector<std::complex<float>> qpskSymbols(binarySequence.size() / 2);

    // Map the phase angles (in radians)
    float phaseAngles[4] = {
        M_PI / 4,     // 45 degrees for "00"
        3 * M_PI / 4, // 135 degrees for "01"
        5 * M_PI / 4, // 225 degrees for "11"
        7 * M_PI / 4  // 315 degrees for "10"
    };

    // Iterate over the Gold sequence in pairs
    for (size_t i = 0; i < binarySequence.size(); i += 2)
    {
        int bit1 = binarySequence[i];
        int bit2 = binarySequence[i + 1];

        // Map the bit pair to an index (00 -> 0, 01 -> 1, 11 -> 2, 10 -> 3)
        int index = (bit1 << 1) | bit2;

        // Get the corresponding phase angle
        float phase = phaseAngles[index];

        // Generate the complex symbol using the phase
        qpskSymbols[i / 2] = std::polar(1.0f, phase); // magnitude 1, angle `phase`
    }

    return qpskSymbols;
}

// Function to generate an m-sequence using a primitive polynomial
std::vector<int> WaveformGenerator::generateMSequence(int n, const std::vector<int> &feedbackPolynomial)
{
    // The m-sequence will have length 2^n - 1
    int sequenceLength = (1 << n) - 1;
    std::vector<int> mSequence(sequenceLength);

    // Initialize the shift register (all ones initially)
    std::vector<int> shiftRegister(n, 1);

    // Generate the m-sequence using feedback from the LFSR
    for (int i = 0; i < sequenceLength; ++i)
    {
        mSequence[i] = shiftRegister.back(); // Output bit is the last bit in the shift register

        // Calculate the new feedback bit by XORing specified bits
        int feedbackBit = 0;
        for (int feedbackIndex : feedbackPolynomial)
        {
            feedbackBit ^= shiftRegister[feedbackIndex - 1]; // XOR the bits based on the feedback polynomial
        }

        // Shift the register and insert the feedback bit
        for (int j = n - 1; j > 0; --j)
        {
            shiftRegister[j] = shiftRegister[j - 1];
        }
        shiftRegister[0] = feedbackBit;
    }

    return mSequence;
}

// Function to generate a Gold sequence with given n and shift
std::vector<int> WaveformGenerator::generateGoldSequence(int n, int shift, const std::vector<int> &feedbackPolynomial1, const std::vector<int> &feedbackPolynomial2)
{
    // Generate the first m-sequence
    std::vector<int> mSequence1 = generateMSequence(n, feedbackPolynomial1);

    // Generate the second m-sequence
    std::vector<int> mSequence2 = generateMSequence(n, feedbackPolynomial2);

    // Apply the shift to the second m-sequence
    std::vector<int> shiftedMSequence2(mSequence2.size());
    for (int i = 0; i < mSequence2.size(); ++i)
    {
        shiftedMSequence2[i] = mSequence2[(i + shift) % mSequence2.size()];
    }

    // XOR the two m-sequences to generate the Gold sequence
    std::vector<int> goldSequence(mSequence1.size());
    for (int i = 0; i < mSequence1.size(); ++i)
    {
        goldSequence[i] = mSequence1[i] ^ shiftedMSequence2[i]; // XOR operation
    }

    return goldSequence;
}

std::vector<std::complex<float>> WaveformGenerator::generate_waveform()
{

    std::vector<std::complex<float>> final_sequence;
    std::vector<std::complex<float>> sequence;

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
        sequence = generateDFTseq();
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

    // add zero at the beginning
    if (wf_pad > 0)
    {
        final_sequence.insert(final_sequence.begin(), wf_pad, std::complex<float>(pad_scale));
        // final_sequence.insert(final_sequence.end(), wf_pad, std::complex<float>(pad_scale));
    }

    return final_sequence;
}