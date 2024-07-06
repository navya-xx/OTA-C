#include "../USRP_class.hpp"
#include "../utility_funcs.hpp"
#include "../ConfigParser.hpp"
#include "../PeakDetection.hpp"
#include "../CycleStartDetector.hpp"
#include <stdexcept>
#include <ncurses.h>

/***************************************************************
 * Copyright (c) 2023 Navneet Agrawal
 *
 * Author: Navneet Agrawal
 * Email: navneet.agrawal@tu-berlin.de
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

void waitForKeyPress()
{
    initscr(); // Initialize the window
    cbreak();  // Disable line buffering
    noecho();  // Do not echo while we do getch

    printw("Press any key to continue...");
    refresh(); // Print it on the real screen
    getch();   // Wait for user input

    endwin(); // End curses mode
}

void producer_thread_func(PeakDetectionClass &peak_det_obj, CycleStartDetector &csd_obj, USRP_class &usrp_classobj, ConfigParser &parser, const std::string &homeDirStr, const float total_runtime)
{
    size_t wf_len = parser.getValue_int("Ref-N-zfc");
    size_t zfc_q = parser.getValue_int("Ref-m-zfc");
    size_t wf_reps = parser.getValue_int("Ref-R-zfc");
    size_t wf_gap = 0;
    double tick_rate = parser.getValue_int("rate");

    auto rx_stream = usrp_classobj.rx_streamer;
    size_t max_rx_packet_size = usrp_classobj.max_rx_packet_size;
    float rate = usrp_classobj.rx_rate;
    const float burst_pkt_time = std::max<float>(0.100f, (2 * max_rx_packet_size / rate));

    const auto start_time = std::chrono::steady_clock::now();
    const auto stop_time = start_time + std::chrono::milliseconds(int64_t(1000 * total_runtime));

    std::vector<std::complex<float>> buff(max_rx_packet_size);
    size_t round = 1;

    while (not stop_signal_called and not(std::chrono::steady_clock::now() > stop_time))
    {
        std::cout << "------------  Round " << round << " -----------------" << std::endl
                  << std::endl;

        uhd::rx_metadata_t md;

        // setup streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.num_samps = max_rx_packet_size;
        stream_cmd.stream_now = true;
        rx_stream->issue_stream_cmd(stream_cmd);

        float recv_timeout = burst_pkt_time + 0.05;
        size_t num_rx_samps;

        // Run this loop until either time expired (if a duration was given), until
        // the requested number of samples were collected (if such a number was
        // given), or until Ctrl-C was pressed.

        while (not csd_success_signal and not stop_signal_called and not(std::chrono::steady_clock::now() > stop_time))
        {
            try
            {
                num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, recv_timeout, false);
                recv_timeout = burst_pkt_time;
            }
            catch (uhd::io_error &e)
            {
                std::cerr << "Caught an IO exception in CSD Receiver Thread. " << std::endl;
                std::cerr << e.what() << std::endl;
                return;
            }

            if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
            {
                std::cout << boost::format("Timeout while streaming") << std::endl;
                break;
            }
            if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW)
            {
                std::cerr << std::endl
                          << "*** Got an overflow indication." << std::endl
                          << std::endl;
                continue;
            }
            if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
            {
                std::string error = str(boost::format("Receiver error: %s") % md.strerror());
                std::cerr << error << std::endl;
            }

            // save data to file for later analysis
            if (is_save_stream_data)
                save_stream_to_file(save_stream_file, outfile, buff);

            csd_obj.produce(buff, num_rx_samps, md.time_spec);
        }

        // issue stop streaming command
        stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
        rx_stream->issue_stream_cmd(stream_cmd);

        if (is_save_stream_data)
        {
            if (outfile.is_open())
                outfile.close();
        }

        std::cout << "Producer finished" << std::endl;

        if (stop_signal_called)
            return;

        // Start information transmission
        if (csd_success_signal and not stop_signal_called)
        {
            std::cout << "Starting transmission after CSD..." << std::endl;

            float e2e_est_ref_sig_amp = csd_obj.e2e_est_ref_sig_amp;
            // adjust tx/rx gains based on tx_scaling factor -> too low, decrease gain, or vice-versa
            // usrp_classobj.gain_adjustment(e2e_est_ref_sig_amp, min_path_loss_dB);

            float tx_scaling_factor = 1.0; // min_e2e_amp / e2e_est_ref_sig_amp;
            std::cout << "Est e2e amp = " << e2e_est_ref_sig_amp << ", Min e2e amp = " << min_e2e_amp << std::endl;

            uhd::time_spec_t tx_start_timer = csd_obj.csd_tx_start_timer;

            usrp_classobj.transmission(tx_zfc_seq, tx_start_timer, false);

            std::cout << "CSD Test TX complete!" << std::endl
                      << std::endl;
        }

        csd_success_signal = false;

        // get peaks info
        std::string save_rx_buffer_filepath = homeDirStr + parser.save_buffer_filename + "_" + std::to_string(round) + ".dat";
        peak_det_obj.save_data_to_file(save_rx_buffer_filepath);

        ++round;
    }
}

void csd_test_consumer_thread(CycleStartDetector &csd_obj, ConfigParser &parser, std::atomic<bool> &csd_success_signal)
{

    float total_runtime = parser.getValue_float("duration");
    if (total_runtime == 0.0)
        total_runtime = 5 * 60; // run for 5 minutes at most
    const auto start_time = std::chrono::steady_clock::now();
    const auto stop_time = start_time + std::chrono::milliseconds(int64_t(1000 * total_runtime));

    while (not stop_signal_called and not(std::chrono::steady_clock::now() > stop_time))
    {
        if (csd_obj.consume(csd_success_signal))
            std::cout << "*** Successful CSD detection!" << std::endl;
    }
}

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    const char *homeDir = std::getenv("HOME");
    std::string homeDirStr(homeDir);

    // rx and tx streamers -- initilize
    ConfigParser parser(homeDirStr + "/OTA-C/cpp/leaf_config.conf");

    if (argc < 2)
        throw std::invalid_argument("ERROR : device address missing! Pass it as first argument to the function call.");

    std::string device_id = argv[1];
    parser.set_value("device-id", device_id, "str", "USRP device number");

    float duration = 5.0;
    if (argc > 2)
        duration = std::stof(argv[2]);

    // Logger
    // Logger logger(homeDirStr + "/OTA-C/cpp/logs/log_" + device_id + ".log", Logger::Level::DEBUG, true);

    parser.print_values();

    // USRP init
    USRP_class usrp_classobj(parser);
    usrp_classobj.initialize();
    uhd::time_spec_t rx_sample_duration = usrp_classobj.rx_sample_duration;

    // CycleStartDetector and PeakDetector class object init
    PeakDetectionClass peak_det_obj(parser, usrp_classobj.init_background_noise);
    CycleStartDetector csd_obj(parser, rx_sample_duration, peak_det_obj);

    waitForKeyPress();

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    // setup thread_group
    boost::thread_group thread_group;

    auto producer_thread = thread_group.create_thread([=, &csd_obj, &peak_det_obj, &parser, &usrp_classobj]()
                                                      { producer_thread_func(peak_det_obj, csd_obj, usrp_classobj, parser, homeDirStr); });

    uhd::set_thread_name(producer_thread, "producer_thread");

    // consumer thread
    auto consumer_thread = thread_group.create_thread([=, &csd_obj, &parser]()
                                                      { consumer_thread_func(csd_obj, parser); });

    uhd::set_thread_name(consumer_thread, "consumer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    // receive continuously and save to file
    auto rx_data = usrp_classobj.reception(0, duration, uhd::time_spec_t(0.0), true);

    return EXIT_SUCCESS;
}