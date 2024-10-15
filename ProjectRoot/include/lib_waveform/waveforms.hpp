#ifndef WAVEFORMS
#define WAVEFORMS

#include "pch.hpp"

#include "log_macros.hpp"

class WaveformGenerator
{
public:
    enum WAVEFORM_TYPE
    {
        ZFC,
        UNIT_RAND,
        IMPULSE,
        DFT,
        SINE
    };

    WaveformGenerator();

    void initialize(WAVEFORM_TYPE wf_type, size_t wf_len, size_t wf_reps = 1, size_t wf_gap = 0, size_t init_wf_pad = 0, size_t zfc_q = 1, float scale = 1.0, size_t rand_seed = 0);

    size_t wf_len, wf_reps, wf_gap, wf_pad, zfc_q, rand_seed;
    float scale = 1.0, pad_scale = 0.0;

    std::vector<std::complex<float>> generate_waveform();

    std::vector<int> feedbackPolynomial11_1 = {11, 8, 5, 2};
    std::vector<int> feedbackPolynomial11_2 = {11, 6, 5, 1};

private:
    WAVEFORM_TYPE wf_type;

    std::vector<std::complex<float>> generateZadoffChuSequence();

    std::vector<std::complex<float>> generateUnitCircleRandom();

    std::vector<std::complex<float>> generateImpulseSignal();

    std::vector<std::complex<float>> generateQPSKSymbols(const std::vector<int> &binarySequence);

    std::vector<int> generateMSequence(int n, const std::vector<int> &feedbackPolynomial);

    std::vector<int> generateGoldSequence(int n, int shift, const std::vector<int> &feedbackPolynomial1 = {11, 8, 5, 2}, const std::vector<int> &feedbackPolynomial2 = {11, 6, 5, 1});

    std::vector<std::complex<float>> generateDFTseq();
};

#endif // WAVEFORMS