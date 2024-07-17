#ifndef FFT_WRAPPER_H
#define FFT_WRAPPER_H

#include "pch.hpp"
#include "log_macros.hpp"
#include <fftw3.h>

class FFTWrapper
{
public:
    FFTWrapper();
    ~FFTWrapper();

    void initialize(size_t size);

    // Perform FFT of input array
    void fft(const std::vector<std::complex<float>> &input,
             std::vector<std::complex<float>> &output);

    // Perform iFFT of input array
    void ifft(const std::vector<std::complex<float>> &input,
              std::vector<std::complex<float>> &output);

    // Zero-pad the input data to a specified length
    void zeroPad(const std::vector<std::complex<float>> &input,
                 std::vector<std::complex<float>> &output, int paddedSize);
    void zeroPad(const std::deque<std::complex<float>> &input,
                 std::vector<std::complex<float>> &output, int paddedSize);

private:
    size_t size_;
    fftw_complex *fft_input_;
    fftw_complex *fft_output_;
    fftw_plan fft_plan_;
    fftw_plan ifft_plan_;
};

#endif // FFT_WRAPPER_H