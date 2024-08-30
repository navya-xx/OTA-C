#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"
#include "usrp_class.hpp"
// #include "FFTWrapper.hpp"
#include "waveforms.hpp"

#define LOG_LEVEL LogLevel::DEBUG
static bool stop_signal_called = false;
std::atomic<bool> atomic_bool(false);
void sig_int_handler(int)
{
    stop_signal_called = true;
    atomic_bool = true;
}

void signal_generator(USRP_class &usrp_obj)
{
    // generate signal that spans full band
    // size_t wf_len = 2 * size_t(usrp_obj.tx_rate);
    // size_t sig_bw = 10;
    // size_t fft_L = 1;
    // while (fft_L < wf_len)
    // {
    //     fft_L *= 2;
    // }
    // int num_FFT_threads = 1;
    // FFTWrapper fftw_wrapper;
    // fftw_wrapper.initialize(fft_L, num_FFT_threads);

    // std::vector<std::complex<float>> freq_seq(fft_L, std::complex<float>(1.0, 1.0)), final_seq;
    // for (int i = 0; i < sig_bw; ++i)
    // {
    //     freq_seq[i] = std::complex<float>(1.0, 1.0);
    //     freq_seq[fft_L - i - 1] = std::complex<float>(1.0, 1.0);
    // }
    // fftw_wrapper.ifft(freq_seq, final_seq);

    // std::vector<std::complex<float>> tx_waveform(final_seq.begin(), final_seq.begin() + wf_len);

    WaveformGenerator wf_gen = WaveformGenerator();

    wf_gen.initialize(wf_gen.ZFC, usrp_obj.max_tx_packet_size, 1, 0, 0, 1, 1.0, 0);
    // wf_gen.initialize(wf_gen.IMPULSE, N_zfc, reps_zfc, 0, wf_pad, q_zfc, 1.0, 0);
    const auto tx_waveform = wf_gen.generate_waveform();

    LOG_INFO_FMT("Tx waveform length = %1%", tx_waveform.size());

    /*------ Calibration setup --------*/

    while (true)
    {
        if (!atomic_bool.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        else
        {
            atomic_bool = false;
            break;
        }
    }

    for (float tx_gain_val = 75.0; tx_gain_val < 90.0; tx_gain_val = tx_gain_val + 1.0)
    {
        usrp_obj.set_tx_gain(tx_gain_val);

        LOG_INFO_FMT("---------------- STARTING TX GAIN : %1% -----------------", tx_gain_val);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        usrp_obj.continuous_transmission(tx_waveform, atomic_bool);

        LOG_INFO_FMT("FINISHED TX GAIN : %1%. Sleeping for 12 secs...", tx_gain_val);
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        atomic_bool = false;

        if (stop_signal_called)
            break;
    }

    LOG_INFO("Calibration Ends!");
};

void input_next_power()
{
    std::string input;
    while (true)
    {
        std::cout << "Press Enter move to next power level... ";
        std::getline(std::cin, input);
        if (!atomic_bool.load())
            atomic_bool = true;

        if (stop_signal_called)
            break;
    }
};

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    /*------ Initialize ---------------*/
    const char *homeDir = std::getenv("HOME");
    std::string homeDirStr(homeDir);
    std::string projectDir = homeDirStr + "/OTA-C/ProjectRoot";
    std::string curr_time_str = currentDateTimeFilename();

    if (argc < 2)
        throw std::invalid_argument("ERROR : device address is missing! Pass it as first argument to the function call.");

    std::string device_id = argv[1];
    // float tx_gain = std::stof(argv[2]);

    /*----- LOG ------------------------*/
    std::string logFileName = projectDir + "/storage/logs/leaf_" + device_id + "_" + curr_time_str + ".log";
    Logger::getInstance().initialize(logFileName);
    Logger::getInstance().setLogLevel(LOG_LEVEL);

    /*------ Parse Config -------------*/
    ConfigParser parser(projectDir + "/config/config.conf");
    parser.set_value("device-id", device_id, "str", "USRP device number");
    // parser.set_value("tx-gain", floatToStringWithPrecision(tx_gain, 1), "float", "USRP device number");

    parser.set_value("storage-folder", projectDir + "/storage", "str", "Location of storage director");

    /*------- USRP setup --------------*/
    USRP_class usrp_obj(parser);

    usrp_obj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_obj.initialize(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    parser.set_value("max-rx-packet-size", std::to_string(usrp_obj.max_rx_packet_size), "int", "Max Rx packet size");

    parser.print_values();

    /*------ Threads - Consumer / Producer --------*/

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    // setup thread_group
    boost::thread_group thread_group;

    auto signal_generator_thread = thread_group.create_thread([=, &usrp_obj]()
                                                              { signal_generator(usrp_obj); });

    uhd::set_thread_name(signal_generator_thread, "signal_generator");

    input_next_power();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    return EXIT_SUCCESS;
};