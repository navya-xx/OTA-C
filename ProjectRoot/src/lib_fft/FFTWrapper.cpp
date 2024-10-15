#include "FFTWrapper.hpp"

FFTWrapper::FFTWrapper() {}

void FFTWrapper::initialize(size_t size, int num_threads)
{
    if (num_threads <= 0)
    {
        LOG_ERROR("Number of threads must be positive.");
    }
    size_ = size;

    // Initialize FFTW with threading
    fftw_init_threads();
    fftw_plan_with_nthreads(num_threads);

    fft_input_ = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * size_);
    fft_output_ = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * size_);
    fft_plan_ = fftw_plan_dft_1d(size_, fft_input_, fft_output_, FFTW_FORWARD, FFTW_ESTIMATE);
    ifft_plan_ = fftw_plan_dft_1d(size_, fft_output_, fft_input_, FFTW_BACKWARD, FFTW_ESTIMATE);
}

FFTWrapper::~FFTWrapper()
{
    fftw_destroy_plan(fft_plan_);
    fftw_destroy_plan(ifft_plan_);
    fftw_free(fft_input_);
    fftw_free(fft_output_);
    fftw_cleanup_threads();
}

void FFTWrapper::fft(const std::vector<std::complex<float>> &input,
                     std::vector<std::complex<float>> &output)
{
    // Check input size
    if (input.size() != size_)
    {
        LOG_ERROR("Input size does not match FFT size.");
    }

    // Copy input to fft_input_
    for (int i = 0; i < size_; ++i)
    {
        fft_input_[i][0] = input[i].real();
        fft_input_[i][1] = input[i].imag();
    }

    // Execute FFT
    fftw_execute(fft_plan_);

    // Copy fft_output_ to output
    output.resize(size_);
    for (int i = 0; i < size_; ++i)
    {
        output[i] = std::complex<float>(fft_output_[i][0], fft_output_[i][1]);
    }
}

void FFTWrapper::ifft(const std::vector<std::complex<float>> &input,
                      std::vector<std::complex<float>> &output)
{
    // Check input size
    if (input.size() != size_)
    {
        LOG_ERROR("Input size does not match FFT size.");
    }

    // Copy input to fft_output_
    for (int i = 0; i < size_; ++i)
    {
        fft_output_[i][0] = input[i].real();
        fft_output_[i][1] = input[i].imag();
    }

    // Execute IFFT
    fftw_execute(ifft_plan_);

    // Scale the output by 1/N (to match the definition of IFFT in FFTW)
    float scale_factor = 1.0 / size_;
    output.resize(size_);
    for (int i = 0; i < size_; ++i)
    {
        output[i] = std::complex<float>(fft_input_[i][0] * scale_factor, fft_input_[i][1] * scale_factor);
    }
}

void FFTWrapper::zeroPad(const std::vector<std::complex<float>> &input,
                         std::vector<std::complex<float>> &output, int paddedSize)
{
    if (paddedSize < input.size())
    {
        LOG_ERROR("Padded size must be greater than or equal to the input size.");
    }

    // Resize output to paddedSize
    output.resize(paddedSize);

    // Copy input to output and zero-pad the rest
    for (int i = 0; i < input.size(); ++i)
    {
        output[i] = input[i];
    }
    for (int i = input.size(); i < paddedSize; ++i)
    {
        output[i] = std::complex<float>(0.0, 0.0);
    }
}

void FFTWrapper::zeroPad(const std::deque<std::complex<float>> &input,
                         std::vector<std::complex<float>> &output, int paddedSize)
{
    if (paddedSize < input.size())
    {
        LOG_ERROR("Padded size must be greater than or equal to the input size.");
    }

    // Resize output to paddedSize
    output.resize(paddedSize);

    // Copy input to output and zero-pad the rest
    for (int i = 0; i < input.size(); ++i)
    {
        output[i] = input[i];
    }
    for (int i = input.size(); i < paddedSize; ++i)
    {
        output[i] = std::complex<float>(0.0, 0.0);
    }
}

// Low-pass filter method
void FFTWrapper::lowPassFilter(const std::vector<std::complex<float>> &inputSignal,
                               std::vector<std::complex<float>> &outputSignal,
                               float cutoffFrequency, float sampleRate)
{
    size_t N = inputSignal.size();

    // Ensure FFTWrapper is initialized with the correct size
    if (N != size_)
    {
        initialize(N);
    }

    // Perform FFT on the input signal
    std::vector<std::complex<float>> frequencyDomain(N);
    fft(inputSignal, frequencyDomain);

    // Apply low-pass filter in the frequency domain
    float nyquist = sampleRate / 2.0f;
    size_t cutoffBin = static_cast<size_t>((cutoffFrequency / nyquist) * (N / 2));

    // Zero out frequencies above the cutoff
    for (size_t i = cutoffBin; i < N - cutoffBin; ++i)
    {
        frequencyDomain[i] = std::complex<float>(0, 0);
    }

    // Perform inverse FFT to get back the filtered signal in the time domain
    ifft(frequencyDomain, outputSignal);
}