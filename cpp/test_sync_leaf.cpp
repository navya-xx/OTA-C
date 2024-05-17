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

extern const bool DEBUG = true;

void csd_test_producer_thread(PeakDetectionClass &peak_det_obj, CycleStartDetector &csd_obj, USRP_class &usrp_classobj, ConfigParser &parser, const size_t &tx_m_zfc, std::atomic<bool> &csd_success_signal, const std::string &homeDirStr)
{
    size_t tx_N_zfc = parser.getValue_int("test-signal-len");
    size_t csd_test_tx_reps = parser.getValue_int("test-tx-reps");
    float tx_reps_gap = parser.getValue_int("tx-gap-millisec") / 1e3;
    float total_runtime = parser.getValue_float("duration");

    auto rx_stream = usrp_classobj.rx_streamer;
    auto tx_stream = usrp_classobj.tx_streamer;
    size_t max_rx_packet_size = usrp_classobj.max_rx_packet_size;
    float rate = usrp_classobj.rx_rate;
    if (total_runtime == 0.0)
        total_runtime = 1 * 3600; // run for one hour at most
    const auto start_time = std::chrono::steady_clock::now();
    const auto stop_time = start_time + std::chrono::milliseconds(int64_t(1000 * total_runtime));
    const float burst_pkt_time = std::max<float>(0.100f, (2 * max_rx_packet_size / rate));

    size_t round = 1;

    bool is_save_stream_data;
    std::ofstream outfile;
    std::string save_stream_file;
    std::istringstream iss(parser.getValue_str("is-save-stream-data"));
    iss >> is_save_stream_data;
    if (is_save_stream_data)
    {
        save_stream_file = homeDirStr + "OTA-C/cpp/storage/stream_data.dat";
        // remove existing file
        std::remove(save_stream_file.c_str());
        outfile.open(save_stream_file, std::ios::out | std::ios::binary | std::ios::app);
    }

    while (not stop_signal_called and not(std::chrono::steady_clock::now() > stop_time))
    {
        if (DEBUG)
        {
            std::cout << "Round " << round << std::endl
                      << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        uhd::rx_metadata_t md;
        bool overflow_message = true;

        // setup streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.num_samps = max_rx_packet_size;
        stream_cmd.stream_now = true;
        rx_stream->issue_stream_cmd(stream_cmd);

        std::vector<std::complex<float>> buff(max_rx_packet_size, std::complex<float>(0.0, 0.0));

        float recv_timeout = burst_pkt_time + 0.05;
        size_t num_rx_samps = 0;

        // Run this loop until either time expired (if a duration was given), until
        // the requested number of samples were collected (if such a number was
        // given), or until Ctrl-C was pressed.

        while (not csd_success_signal and not stop_signal_called and not(std::chrono::steady_clock::now() > stop_time))
        {
            try
            {
                num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, recv_timeout, false);
                // recv_timeout = burst_pkt_time;
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
                if (overflow_message)
                {
                    overflow_message = false;
                    std::cerr << "*** Got an overflow indication." << std::endl
                              << std::endl;
                    ;
                }
                continue;
            }
            if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
            {
                std::string error = str(boost::format("Receiver error: %s") % md.strerror());
                std::cerr << error << std::endl;
            }

            // pass on to the cycle start detector
            // std::cout << "Received " << num_rx_samps << " samples. Producing..." << std::endl;

            // save data to file for later analysis
            if (is_save_stream_data)
                save_stream_to_file(save_stream_file, outfile, buff);

            csd_obj.produce(buff, num_rx_samps, md.time_spec, csd_success_signal);
        }
        // issue stop streaming command
        stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
        rx_stream->issue_stream_cmd(stream_cmd);

        std::cout << "Producer finished" << std::endl;

        if (stop_signal_called or std::chrono::steady_clock::now() > stop_time)
            return;

        // Start information transmission
        if (csd_success_signal and not stop_signal_called)
        {
            if (DEBUG)
                std::cout << "Starting transmission after CSD..." << std::endl;

            float ch_pow = csd_obj.ch_pow;
            uhd::time_spec_t tx_start_timer = csd_obj.csd_tx_start_timer;
            float min_ch_pow = parser.getValue_float("min-ch-pow");
            float tx_scaling_factor = min_ch_pow / ch_pow;
            if (tx_scaling_factor > 1)
                std::cerr << "(min_ch_pow) " << min_ch_pow << " > " << ch_pow << " (est avg ch pow)" << std::endl;
            auto tx_zfc_seq = generateZadoffChuSequence(tx_N_zfc, tx_m_zfc, tx_scaling_factor);
            float tx_duration = tx_zfc_seq.size();
            tx_duration = tx_duration / usrp_classobj.tx_sample_duration.get_real_secs();

            // start tx process
            uhd::tx_metadata_t txmd;
            txmd.start_of_burst = true;
            txmd.end_of_burst = false;
            txmd.has_time_spec = true;
            txmd.time_spec = tx_start_timer;
            float timeout = (tx_start_timer - usrp_classobj.usrp->get_time_now()).get_real_secs() + tx_duration;

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

            // std::cout << std::endl
            //           << ": Waiting for async burst ACK... " << std::flush;
            // uhd::async_metadata_t async_md;
            // bool got_async_burst_ack = false;
            // // loop through all messages for the ACK packet (may have underflow messages in queue)
            // while (not got_async_burst_ack and tx_stream->recv_async_msg(async_md, timeout))
            // {
            //     got_async_burst_ack =
            //         (async_md.event_code == uhd::async_metadata_t::EVENT_CODE_BURST_ACK);
            // }
            // std::cout << (got_async_burst_ack ? "success" : "fail") << std::endl
            //           << std::endl;

            // std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
    bool result;

    float total_runtime = parser.getValue_float("duration");
    if (total_runtime == 0.0)
        total_runtime = 1 * 3600; // run for one hour at most
    const auto start_time = std::chrono::steady_clock::now();
    const auto stop_time = start_time + std::chrono::milliseconds(int64_t(1000 * total_runtime));

    while (not stop_signal_called and not(std::chrono::steady_clock::now() > stop_time))
    {
        result = csd_obj.consume(csd_success_signal);

        // check result
        if (result)
        {
            csd_success_signal = true;
            if (DEBUG)
                std::cout << "*** Successful CSD detection!" << std::endl;
        }
        else
        {
            csd_success_signal = false;
        }
    }
}

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    const char *homeDir = std::getenv("HOME");
    std::string homeDirStr(homeDir);

    // rx and tx streamers -- initilize
    ConfigParser parser(homeDirStr + "/OTA-C/cpp/leaf_config.conf");

    std::string args = parser.getValue_str("args");
    if (args == "NULL")
    {
        if (argc < 2)
            throw std::invalid_argument("ERROR : device address missing!");

        args = argv[1];
        parser.set_value("args", args, "str");
    }

    parser.print_values();

    // save rx buffer to a file
    bool is_save_rx_buffer = parser.is_save_buffer();

    // USRP init
    USRP_class usrp_classobj(parser);
    usrp_classobj.initialize();
    parser.set_value("max-rx-packet-size", std::to_string(usrp_classobj.max_rx_packet_size), "int");
    parser.set_value("max-tx-packet-size", std::to_string(usrp_classobj.max_tx_packet_size), "int");

    // create PeakDetector and CycleStartDetector class objects
    PeakDetectionClass peak_det_obj(parser, usrp_classobj.init_background_noise, is_save_rx_buffer);

    uhd::time_spec_t rx_sample_duration = usrp_classobj.rx_sample_duration;
    CycleStartDetector csd_obj(parser, rx_sample_duration, peak_det_obj);

    //----------- THREADS - producer (Rx/Tx) thread and consumer thread ---------------------
    std::atomic<bool> csd_success_signal(false);

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    if (argc < 3)
        throw std::invalid_argument("ERROR : TX ZFC device identifier `m´ missing !");
    size_t tx_m_zfc = std::stoi(argv[2]);

    // setup thread_group
    boost::thread_group thread_group;

    auto producer_thread = thread_group.create_thread([=, &csd_obj, &peak_det_obj, &parser, &usrp_classobj, &csd_success_signal]()
                                                      { csd_test_producer_thread(peak_det_obj, csd_obj, usrp_classobj, parser, tx_m_zfc, csd_success_signal, homeDirStr); });

    uhd::set_thread_name(producer_thread, "producer_thread");

    // consumer thread
    auto consumer_thread = thread_group.create_thread([=, &csd_obj, &parser, &csd_success_signal]()
                                                      { csd_test_consumer_thread(csd_obj, parser, csd_success_signal); });

    uhd::set_thread_name(consumer_thread, "consumer_thread");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread_group.join_all();

    return EXIT_SUCCESS;
}