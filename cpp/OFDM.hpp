#ifndef OFDM_HPP
#define OFDM_HPP

#include <vector>
#include <complex>
#include <cmath>

class OFDM
{
public:
    OFDM(size_t N, size_t CP);

    std::vector<std::complex<float>> fft(const std::vector<std::complex<float>> &timeDomainSymbols);
    std::vector<std::complex<float>> ifft(const std::vector<std::complex<float>> &freqDomainSymbols);
    std::vector<std::complex<float>> addCP(const std::vector<std::complex<float>> &timeDomainSymbols);
    std::vector<std::complex<float>> removeCP(const std::vector<std::complex<float>> &symbolsWithCP);

    std::vector<std::vector<std::complex<float>>> segmentData(const std::vector<std::complex<float>> &data);
    std::vector<std::complex<float>> mapToSubcarriers(const std::vector<std::complex<float>> &qamSymbols, const bool dc_offset_subcarrier_index = false);

private:
    size_t N;
    size_t CP;

    void fftRecursive(std::vector<std::complex<float>> &a, bool invert);
};

#endif // OFDM_HPP