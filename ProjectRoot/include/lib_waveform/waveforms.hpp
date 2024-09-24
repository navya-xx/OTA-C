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
    float scale = 1.0, pad_scale = 0.0005;

    std::vector<std::complex<float>> generate_waveform();

private:
    WAVEFORM_TYPE wf_type;

    std::vector<std::complex<float>> generateZadoffChuSequence();

    std::vector<std::complex<float>> generateUnitCircleRandom();

    std::vector<std::complex<float>> generateImpulseSignal();

    std::vector<std::complex<float>> generateDFTseq();
};

#endif // WAVEFORMS