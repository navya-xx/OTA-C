#include "USRP_class.hpp"
#include "utility_funcs.hpp"
#include "ConfigParser.hpp"
#include "PeakDetection.hpp"
#include "CycleStartDetector.hpp"
#include <stdexcept>

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

extern const bool DEBUG = false;

void csd_test_producer_thread(PeakDetectionClass &peak_det_obj, CycleStartDetector &csd_obj, USRP_class &usrp_classobj, ConfigParser &parser, std::atomic<bool> &csd_success_signal, const std::string &homeDirStr)
{
    size_t tx_N_zfc = parser.getValue_int("test-signal-len");
    size_t tx_m_zfc = parser.getValue_int("tx-m-zfc");
    size_t csd_test_tx_reps = parser.getValue_int("test-tx-reps");
    // float tx_reps_gap = parser.getValue_int("tx-gap-millisec") / 1e3;
    float total_runtime = parser.getValue_float("duration");
    float min_ch_pow = parser.getValue_float("min-ch-pow");

    auto rx_stream = usrp_classobj.rx_streamer;
    auto tx_stream = usrp_classobj.tx_streamer;
    size_t max_rx_packet_size = usrp_classobj.max_rx_packet_size;
    float rate = usrp_classobj.rx_rate;
    const float burst_pkt_time = std::max<float>(0.100f, (2 * max_rx_packet_size / rate));

    if (total_runtime == 0.0)
        total_runtime = 5 * 60; // run for 5 minutes at most
    const auto start_time = std::chrono::steady_clock::now();
    const auto stop_time = start_time + std::chrono::milliseconds(int64_t(1000 * total_runtime));

    size_t round = 1;
    bool is_save_stream_data = false;
    if (parser.getValue_str("is-save-stream-data") == "true")
        is_save_stream_data = true;

    std::ofstream outfile;
    std::string save_stream_file;
    if (is_save_stream_data)
    {
        save_stream_file = homeDirStr + "/OTA-C/cpp/storage/stream_data.dat";
        // remove existing file
        std::remove(save_stream_file.c_str());
        outfile.open(save_stream_file, std::ios::out | std::ios::binary | std::ios::app);
    }

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

        std::vector<std::complex<float>> buff(max_rx_packet_size, std::complex<float>(0.0, 0.0));

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

        std::cout << "Producer finished" << std::endl;

        if (stop_signal_called)
            return;

        // Start information transmission
        if (csd_success_signal and not stop_signal_called)
        {
            std::cout << "Starting transmission after CSD..." << std::endl;

            float ch_pow = csd_obj.ch_pow;
            float tx_scaling_factor = min_ch_pow / ch_pow;
            if (tx_scaling_factor > 1)
                std::cerr << "(min_ch_pow) " << min_ch_pow << " > " << ch_pow << " (est avg ch pow)" << std::endl;
            auto tx_zfc_seq = generateZadoffChuSequence(tx_N_zfc, tx_m_zfc, tx_scaling_factor);
            float tx_duration = tx_zfc_seq.size() / usrp_classobj.tx_rate;

            // start tx process
            uhd::tx_metadata_t txmd;
            txmd.start_of_burst = true;
            txmd.end_of_burst = false;
            float timeout;

            // match time at both leaf and cent
            uhd::time_spec_t tx_start_timer = csd_obj.csd_tx_start_timer;
            if (tx_start_timer < usrp_classobj.usrp->get_time_now())
            {
                std::cerr << "tx-wait-microsec is not enough. Consider increasing tx-wait-microsec!" << std::endl;
                txmd.has_time_spec = true;
                timeout = tx_duration + 0.05;
            }
            else
            {
                txmd.has_time_spec = true;
                txmd.time_spec = tx_start_timer;
                timeout = (tx_start_timer - usrp_classobj.usrp->get_time_now()).get_real_secs() + tx_duration;
            }

            for (size_t i = 0; i < csd_test_tx_reps; ++i)
            {
                if (stop_signal_called)
                    return;

                size_t num_tx_samps = tx_stream->send(&tx_zfc_seq.front(), tx_zfc_seq.size(), txmd, timeout);
                timeout = tx_duration;
                txmd.start_of_burst = false;
                txmd.has_time_spec = false;

                if (num_tx_samps < tx_zfc_seq.size())
                    std::cerr << "Transmission " << i << " timed-out!!" << std::endl;
                else
                    std::cout << "Transmission number " << i << " over." << std::endl;

                // tx_start_timer = usrp_classobj.usrp->get_time_now() + uhd::time_spec_t(tx_reps_gap);
            }

            txmd.end_of_burst = true;
            tx_stream->send("", 0, txmd);

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
    if (argc < 3)
        throw std::invalid_argument("ERROR : TX ZFC device identifier `m´ missing !");

    std::string device_id = argv[1];
    parser.set_value("device-id", device_id, "str", "USRP device number");
    size_t tx_m_zfc = std::stoi(argv[2]);
    parser.set_value("tx-m-zfc", std::to_string(tx_m_zfc), "int", "Test signal ZFC seq param m");

    // Logger
    // Logger logger(homeDirStr + "/OTA-C/cpp/logs/log_" + device_id + ".log", Logger::Level::DEBUG, true);

    parser.print_values();

    // USRP init
    USRP_class usrp_classobj(parser);
    usrp_classobj.initialize();
    parser.set_value("max-rx-packet-size", std::to_string(usrp_classobj.max_rx_packet_size), "int");
    parser.set_value("max-tx-packet-size", std::to_string(usrp_classobj.max_tx_packet_size), "int");
    uhd::time_spec_t rx_sample_duration = usrp_classobj.rx_sample_duration;

    //----------- CycleStartDetector and PeakDetector classes ---------------------

    // create PeakDetector and CycleStartDetector class objects
    PeakDetectionClass peak_det_obj(parser, usrp_classobj.init_background_noise);
    CycleStartDetector csd_obj(parser, rx_sample_duration, peak_det_obj);

    //----------- THREADS - producer (Rx/Tx) thread and consumer thread ---------------------
    std::atomic<bool> csd_success_signal(false);

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    // setup thread_group
    boost::thread_group thread_group;

    auto producer_thread = thread_group.create_thread([=, &csd_obj, &peak_det_obj, &parser, &usrp_classobj, &csd_success_signal]()
                                                      { csd_test_producer_thread(peak_det_obj, csd_obj, usrp_classobj, parser, csd_success_signal, homeDirStr); });

    uhd::set_thread_name(producer_thread, "producer_thread");

    // consumer thread
    auto consumer_thread = thread_group.create_thread([=, &csd_obj, &parser, &csd_success_signal]()
                                                      { csd_test_consumer_thread(csd_obj, parser, csd_success_signal); });

    uhd::set_thread_name(consumer_thread, "consumer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    return EXIT_SUCCESS;
}