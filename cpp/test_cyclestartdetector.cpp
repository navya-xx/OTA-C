#include "CycleStartDetector.hpp"
#include "utility_funcs.hpp"
#include "usrp_routines.hpp"

/***************************************************************
 * Copyright (c) 2023 Navneet Agrawal
 *
 * Author: Navneet Agrawal
 * Email: navneet.agrawal@hhi.fraunhofer.de
 *
 * This code is licensed under the MIT License.
 *
 * For more information, see https://opensource.org/licenses/MIT
 **************************************************************/

namespace po = boost::program_options;
using namespace std::chrono_literals;
static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

extern const bool DEBUG = true;

int UHD_SAFE_MAIN(int argc, char *argv[])
{

    // rx and tx streamers -- initilize
    ConfigParser parser;
    parser.parse("../leaf_config.conf");

    parser.print_values();

    // USRP init
    std::string args = parser.getValue_str("args");
    if (DEBUG)
        std::cout << "USRP = " << args << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    if (DEBUG)
        std::cout << "USRP device setup -> Done!" << std::endl;

    // setup and create streamers
    uhd::tx_streamer::sptr tx_streamer;
    uhd::rx_streamer::sptr rx_streamer;

    auto streamers = create_usrp_streamers(usrp, parser);
    rx_streamer = streamers.first;
    tx_streamer = streamers.second;

    // create data stream type based on config
    // -- we skip this for now

    // get initial noise level
    float init_noise_level = get_background_noise_level(usrp, rx_streamer);
    if (DEBUG)
        std::cout << "Noise level = " << init_noise_level << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // protocol config
    bool keep_running = true;
    float total_time = parser.getValue_float("duration");
    if (total_time == 0.0)
    {
        std::signal(SIGINT, &sig_int_handler);
        std::cout << "Press Ctrl + C to stop streaming..." << std::endl;
        total_time = 24 * 3600; // run for one day at most
    }

    // Cycle Start Detector config
    float rate = parser.getValue_float("rate");
    size_t capacity_mul = parser.getValue_int("capacity-mul") < 1 ? 1 : parser.getValue_int("capacity-mul");
    size_t num_samp_corr = parser.getValue_int("num-samp-corr");
    size_t max_sample_size = rx_streamer->get_max_num_samps();
    size_t capacity = capacity_mul * std::max(num_samp_corr, max_sample_size);
    float sample_duration = usrp->get_rx_rate(parser.getValue_int("channel")) / 1e6;
    size_t Ref_N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t Ref_m_zfc = parser.getValue_int("Ref-m-zfc");
    size_t Ref_R_zfc = parser.getValue_int("Ref-R-zfc");
    float pnr_threshold = parser.getValue_float("pnr-threshold");

    if (num_samp_corr == 0)
        num_samp_corr = Ref_N_zfc * Ref_R_zfc;

    CycleStartDetector<std::complex<float>> csdbuffer(capacity, sample_duration, num_samp_corr, Ref_N_zfc, Ref_m_zfc, Ref_R_zfc, init_noise_level, pnr_threshold);

    std::string file = parser.getValue_str("file");
    if (file != "")
        csdbuffer.save_buffer_flag = true;

    // Threading -- consumer-producer routine
    // atomic bool to signal thread stop
    std::atomic<bool> stop_thread_signal(false);
    const auto start_time = std::chrono::steady_clock::now();
    const auto stop_time =
        start_time + std::chrono::milliseconds(int64_t(1000 * total_time));

    if (DEBUG)
    {
        auto stop_duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(int64_t(total_time)));
        print_duration(stop_duration);
    }

    // setup thread_group
    boost::thread_group thread_group;

    // Rx producer thread
    auto rx_producer_thread = thread_group.create_thread([=, &csdbuffer, &stop_thread_signal]()
                                                         { cyclestartdetector_receiver_thread(csdbuffer, rx_streamer, stop_thread_signal, stop_signal_called, stop_time, rate); });

    uhd::set_thread_name(rx_producer_thread, "csd_rx_producer_thread");

    uhd::time_spec_t csd_detect_time;
    float csd_ch_pow;

    // CSD consumer thread
    auto rx_consumer_thread = thread_group.create_thread([=, &csdbuffer, &csd_detect_time, &csd_ch_pow, &stop_thread_signal]()
                                                         { cyclestartdetector_transmitter_thread(csdbuffer, rx_streamer, stop_thread_signal, stop_signal_called, stop_time, rate, csd_detect_time, csd_ch_pow); });

    uhd::set_thread_name(rx_consumer_thread, "csd_rx_consumer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    size_t num_peaks_detected = csdbuffer.get_peakscount();
    if (num_peaks_detected < Ref_R_zfc)
    {
        std::cout << "CSD failed! Only detected " << num_peaks_detected << " peaks." << std::endl;
        stop_signal_called = true;
    }
    else
        std::cout << "CSD Successful! All  " << num_peaks_detected << " peaks detected." << std::endl;

    if (stop_signal_called)
        return EXIT_FAILURE;

    // Timer analysis
    auto peak_indices = csdbuffer.get_peakindices();
    auto peak_times = csdbuffer.get_timestamps();
    auto peak_vals = csdbuffer.get_peakvals();
    float ch_pow = 0.0;
    for (int i = 0; i < num_peaks_detected; ++i)
    {
        if (i == num_peaks_detected - 1)
            continue;
        else
        {
            std::cout << "Peak " << i + 2 << " and " << i + 1;
            std::cout << "Index diff = " << peak_indices[i + 1];
            std::cout << ", Time diff = " << (peak_times[i + 1] - peak_times[i]).get_real_secs() * 1e6 << std::endl;
        }
        ch_pow += peak_vals[i];
        std::cout << "Peak " << i + 1 << " channel power = " << peak_vals[i] << std::endl;
    }

    ch_pow = ch_pow / num_peaks_detected;

    std::cout << "Avg channel power " << ch_pow << std::endl;

    return 0;
}