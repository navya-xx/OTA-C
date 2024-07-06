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
    };

    std::vector<std::complex<float>> generate_waveform(WAVEFORM_TYPE wf_type, size_t wf_len, size_t wf_reps = 1, size_t wf_gap = 0, size_t zfc_q = 1, float scale = 1.0, size_t rand_seed = 0, bool is_pad_ends = false);

private:
    std::vector<std::complex<float>> generateZadoffChuSequence(size_t wf_len, size_t zfc_q, float scale = 1.0);

    std::vector<std::complex<float>> generateUnitCircleRandom(size_t rand_seed, size_t wf_len, float scale = 1.0);

    std::vector<std::complex<float>> generateImpulseSignal(size_t wf_len, float scale = 1.0);
};

#endif // WAVEFORMS