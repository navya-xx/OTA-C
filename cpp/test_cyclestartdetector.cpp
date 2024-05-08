#include "CycleStartDetector.hpp"
#include "utility_funcs.hpp"
#include "usrp_routines.hpp"
#include "PeakDetection.hpp"
#include <stdexcept>

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

    const char *homeDir = std::getenv("HOME");
    std::string currentDir(homeDir);

    // rx and tx streamers -- initilize
    ConfigParser parser;
    parser.parse(currentDir + "/OTA-C/cpp/leaf_config.conf");

    parser.print_values();

    // USRP init
    std::string args = parser.getValue_str("args");
    if (args == "NULL")
    {
        if (argc < 3)
            throw std::invalid_argument("ERROR : device address missing!");

        args = argv[1];
    }

    const std::string save_ref_rx = parser.getValue_str("save-ref-rx");
    bool save_buffer_flag = true;
    if (save_ref_rx == "NO")
    {
        save_buffer_flag = false;
        if (DEBUG)
            std::cout << "Do not save Ref signal." << std::endl;
    }
    else
    {
        std::string device_id = args;
        for (char &ch : device_id)
        {
            if (ch == '=')
            {
                ch = '_'; // Replace '=' with '_'
            }
        }

        std::string file = currentDir + "/OTA-C/cpp/storage/save_ref_rx_" + device_id;
        if (DEBUG)
            std::cout << "Ref signal save to file " << file << std::endl;
    }

    if (DEBUG)
        std::cout << "USRP = " << args << std::endl;

    uhd::usrp::multi_usrp::sptr usrp;
    bool device_create = false;

    for (int i = 0; i < 4; ++i)
    {
        try
        {
            usrp = uhd::usrp::multi_usrp::make(args);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            device_create = true;
            break;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
        }
    }

    if (not device_create)
    {
        std::cerr << "ERROR: Failed to create device.. Exiting!" << std::endl;
        return EXIT_FAILURE;
    }

    usrp->set_time_now(uhd::time_spec_t(0.0));

    if (DEBUG)
        std::cout << "USRP device setup -> Done!" << std::endl;

    // setup and create streamers
    uhd::tx_streamer::sptr tx_streamer;
    uhd::rx_streamer::sptr rx_streamer;

    auto streamers = create_usrp_streamers(usrp, parser);
    rx_streamer = streamers.first;
    tx_streamer = streamers.second;

    // create scope for time sync process
    uhd::time_spec_t detect_time;
    float ch_pow = 0.0;
    {

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
        size_t max_sample_size = rx_streamer->get_max_num_samps();
        float sample_duration = 1 / usrp->get_rx_rate();
        size_t Ref_N_zfc = parser.getValue_int("Ref-N-zfc");
        size_t Ref_m_zfc = parser.getValue_int("Ref-m-zfc");
        size_t Ref_R_zfc = parser.getValue_int("Ref-R-zfc");
        size_t num_samp_corr = Ref_N_zfc * 1;
        size_t capacity = capacity_mul * std::max(num_samp_corr, max_sample_size);
        float pnr_threshold = parser.getValue_float("pnr-threshold");
        size_t peak_det_tol = parser.getValue_int("peak-det-tol");
        size_t sync_with_peak_from_last = parser.getValue_int("sync-with-peak-from-last");
        float max_peak_mul = parser.getValue_float("max-peak-mul");

        // peak detection class obj init
        PeakDetectionClass peak_det_obj(Ref_N_zfc, Ref_R_zfc, pnr_threshold, init_noise_level, save_buffer_flag, peak_det_tol, max_peak_mul, sync_with_peak_from_last);
        CycleStartDetector csdbuffer(capacity, sample_duration, num_samp_corr, Ref_N_zfc, Ref_m_zfc, Ref_R_zfc, peak_det_obj);

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

        // set usrp timer to zero
        usrp->set_time_now(uhd::time_spec_t(0.0));

        // Rx producer thread
        auto rx_producer_thread = thread_group.create_thread([=, &csdbuffer, &peak_det_obj, &stop_thread_signal]()
                                                             { cyclestartdetector_receiver_thread(csdbuffer, rx_streamer, stop_thread_signal, stop_signal_called, stop_time, rate); });

        uhd::set_thread_name(rx_producer_thread, "csd_rx_producer_thread");

        // CSD consumer thread
        auto rx_consumer_thread = thread_group.create_thread([=, &csdbuffer, &peak_det_obj, &stop_thread_signal]()
                                                             { cyclestartdetector_transmitter_thread(csdbuffer, rx_streamer, stop_thread_signal, stop_signal_called, stop_time, rate); });

        uhd::set_thread_name(rx_consumer_thread, "csd_rx_consumer_thread");

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        thread_group.join_all();

        size_t num_peaks_detected = peak_det_obj.peaks_count;
        if (num_peaks_detected < Ref_R_zfc)
        {
            std::cout << "CSD failed! Only detected " << num_peaks_detected << " peaks." << std::endl;
            stop_signal_called = true;
        }
        else
            std::cout << "CSD Successful! All  " << num_peaks_detected << " peaks detected." << std::endl;

        peak_det_obj.save_complex_data_to_file(file);

        peak_det_obj.print_peaks_data();

        if (stop_signal_called)
            return EXIT_FAILURE;

        // Extract peak data
        ch_pow = peak_det_obj.get_avg_ch_pow();
        detect_time = peak_det_obj.get_sync_time() + uhd::time_spec_t(sample_duration * 2 * Ref_N_zfc);
    }

    // ------------------------------------------------------------------------------------------------------
    // ----- Transmit a ZFC sequence for csd testing --------------

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    size_t Tx_N_zfc = parser.getValue_int("test-signal-len");
    float min_ch_pow = parser.getValue_float("min-ch-pow");
    float tx_wait_microsec = parser.getValue_float("tx-wait-microsec");
    size_t Tx_m_zfc;
    if (argc > 1)
        Tx_m_zfc = static_cast<size_t>(std::stoi(argv[2]));
    else
        std::cerr << "ERROR : m_zfc (prime) must be specified for testing CSD." << std::endl;

    csdtest_tx_leaf_node(usrp, tx_streamer, Tx_N_zfc, Tx_m_zfc, ch_pow, detect_time, min_ch_pow, tx_wait_microsec);

    return 0;
}