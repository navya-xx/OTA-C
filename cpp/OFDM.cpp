#include "OFDM.hpp"
#include <algorithm>

OFDM::OFDM(size_t N, size_t CP) : N(N), CP(CP) {}

void OFDM::fftRecursive(std::vector<std::complex<float>> &a, bool invert)
{
    size_t n = a.size();
    if (n <= 1)
        return;

    std::vector<std::complex<float>> a0(n / 2), a1(n / 2);
    for (size_t i = 0; 2 * i < n; ++i)
    {
        a0[i] = a[i * 2];
        a1[i] = a[i * 2 + 1];
    }

    fftRecursive(a0, invert);
    fftRecursive(a1, invert);

    float angle = 2 * M_PI / n * (invert ? -1 : 1);
    std::complex<float> w(1), wn(cos(angle), sin(angle));
    for (size_t i = 0; 2 * i < n; ++i)
    {
        a[i] = a0[i] + w * a1[i];
        a[i + n / 2] = a0[i] - w * a1[i];
        if (invert)
        {
            a[i] /= 2;
            a[i + n / 2] /= 2;
        }
        w *= wn;
    }
}

std::vector<std::complex<float>> OFDM::fft(const std::vector<std::complex<float>> &timeDomainSymbols)
{
    std::vector<std::complex<float>> freqDomainSymbols = timeDomainSymbols;
    fftRecursive(freqDomainSymbols, false);
    return freqDomainSymbols;
}

std::vector<std::complex<float>> OFDM::ifft(const std::vector<std::complex<float>> &freqDomainSymbols)
{
    std::vector<std::complex<float>> timeDomainSymbols = freqDomainSymbols;
    fftRecursive(timeDomainSymbols, true);
    return timeDomainSymbols;
}

std::vector<std::complex<float>> OFDM::addCP(const std::vector<std::complex<float>> &timeDomainSymbols)
{
    std::vector<std::complex<float>> symbolsWithCP(N + CP);
    // Copy CP part
    std::copy(timeDomainSymbols.end() - CP, timeDomainSymbols.end(), symbolsWithCP.begin());
    // Copy original symbols
    std::copy(timeDomainSymbols.begin(), timeDomainSymbols.end(), symbolsWithCP.begin() + CP);
    return symbolsWithCP;
}

std::vector<std::complex<float>> OFDM::removeCP(const std::vector<std::complex<float>> &symbolsWithCP)
{
    std::vector<std::complex<float>> timeDomainSymbols(N);
    // Remove CP part
    std::copy(symbolsWithCP.begin() + CP, symbolsWithCP.end(), timeDomainSymbols.begin());
    return timeDomainSymbols;
}

std::vector<std::vector<std::complex<float>>> OFDM::segmentData(const std::vector<std::complex<float>> &data)
{
    std::vector<std::vector<std::complex<float>>> segments;
    size_t blockSize = N; // Block size equals the number of subcarriers
    size_t numSegments = (data.size() + blockSize - 1) / blockSize;
    for (size_t i = 0; i < numSegments; ++i)
    {
        size_t start = i * blockSize;
        size_t end = std::min(start + blockSize, data.size());
        segments.push_back(std::vector<std::complex<float>>(data.begin() + start, data.begin() + end));
    }
    return segments;
}

std::vector<std::complex<float>> OFDM::mapToSubcarriers(const std::vector<std::complex<float>> &freq_domain_symbols, const bool skip_dc_offset_subcarrier)
{
    std::vector<std::complex<float>> subcarriers(N, 0);
    size_t dataSize = freq_domain_symbols.size();
    size_t subcarrierIndex = 0; // Start from index 1, skipping the DC offset subcarrier at index 0
    size_t dc_offset_subcarrier_index = N / 2 - 1;
    for (size_t i = 0; i < dataSize; ++i)
    {
        if (skip_dc_offset_subcarrier and i == dc_offset_subcarrier_index) // Wrap around to skip the DC offset subcarrier
            continue;

        subcarriers[subcarrierIndex++] = freq_domain_symbols[i];
    }
    return subcarriers;
}