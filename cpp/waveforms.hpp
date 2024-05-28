#pragma once
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <uhd/convert.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/date_time.hpp>
#include <atomic>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <csignal>
#include <fstream>
#include <thread>
#include <iomanip>
#include <sstream>
#include <deque>
#include <map>
#include <unordered_map>
#include <random>
#include <cmath>

class WaveformGenerator
{
public:
    enum WAVEFORM_TYPE
    {
        ZFC,
        UNIT_RAND,
        IMPULSE,
    };

    std::vector<std::complex<float>> generate_waveform(WAVEFORM_TYPE wf_type, size_t wf_len, size_t wf_reps = 1, size_t wf_gap = 0, size_t zfc_q = 1, float scale = 1.0, size_t rand_seed = 0, bool is_cyclic_padding = false);

private:
    std::vector<std::complex<float>> generateZadoffChuSequence(size_t wf_len, size_t zfc_q, float scale = 1.0);

    std::vector<std::complex<float>> generateUnitCircleRandom(size_t rand_seed, size_t wf_len, float scale = 1.0);

    std::vector<std::complex<float>> generateImpulse(size_t wf_len, float scale = 1.0);
};