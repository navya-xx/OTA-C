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

int UHD_SAFE_MAIN(int argc, char *argv[])
{

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;

    const char *homeDir = std::getenv("HOME");
    std::string currentDir(homeDir);

    // rx and tx streamers -- initilize
    ConfigParser parser(currentDir + "/OTA-C/cpp/leaf_config.conf");

    std::string args = parser.getValue_str("args");
    if (args == "NULL")
    {
        if (argc < 2)
            throw std::invalid_argument("ERROR : device address missing!");

        args = argv[1];
        parser.set_value("args", args, "str");
    }

    parser.print_values();

    // file to save Rx symbols
    std::string filename = parser.getValue_str("test-file");
    if (filename == "NULL")
    {
        if (argc < 3)
        {
            filename = currentDir + "/OTA-C/cpp/storage/cent_test_def";
            std::cerr << "Filename not specified. Using default filename : " << filename << std::endl;
        }
        else
        {
            filename = currentDir + "/OTA-C/cpp/storage/" + argv[2];
        }
    }
    else
    {
        filename = currentDir + "/OTA-C/cpp/storage/" + filename;
    }

    // USRP init
    USRP_class usrp_classobj(parser);
    usrp_classobj.initialize();

    // prepare to transmit ZFC seq
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t m_zfc = parser.getValue_int("Ref-m-zfc");
    size_t R_zfc = parser.getValue_int("Ref-R-zfc");

    size_t ch_N_zfc = parser.getValue_int("ch-seq-len");
    size_t ch_M_zfc = parser.getValue_int("ch-seq-M");

    auto zfc_seq = generateZadoffChuSequence(N_zfc, m_zfc);
    auto ch_zfc_seq = generateZadoffChuSequence(ch_N_zfc, ch_M_zfc);

    size_t ch_seq_reps = 2;
    size_t ref_ch_gap = 1 * ch_N_zfc;

    std::vector<std::complex<float>> buff(N_zfc * R_zfc + ref_ch_gap + ch_seq_reps * ch_N_zfc, std::complex<float>(0.0, 0.0));
    for (int i = 0; i < N_zfc * R_zfc; ++i)
    {
        buff[i] = zfc_seq[i % N_zfc];
    }

    if (ch_seq_reps > 0)
    {
        for (int i = 0; i < ch_seq_reps * ch_N_zfc; ++i)
        {
            buff[i + N_zfc * R_zfc + ref_ch_gap] = ch_zfc_seq[i % ch_N_zfc];
        }
    }

    float total_runtime = parser.getValue_float("duration");
    if (total_runtime == 0.0)
        total_runtime = 1 * 3600; // run for one hour at most
    const auto start_time = std::chrono::steady_clock::now();
    const auto stop_time = start_time + std::chrono::milliseconds(int64_t(1000 * total_runtime));
    size_t csd_wait_time_millisec = parser.getValue_int("csd-wait-time-millisec");

    float tx_wait_time = parser.getValue_float("tx-wait-microsec");
    size_t test_signal_len = parser.getValue_int("test-signal-len");
    float sample_duration = usrp_classobj.tx_sample_duration.get_real_secs();
    size_t test_tx_reps = parser.getValue_int("test-tx-reps");
    float tx_reps_gap = parser.getValue_int("tx-gap-millisec") / 1e3;

    // size_t num_rx_samps = test_tx_reps * (test_signal_len + tx_reps_gap * sample_duration) + 10 * test_signal_len;
    size_t num_rx_samps = test_tx_reps * test_signal_len + 10 * test_signal_len;

    int iter_counter = 1;

    while (not stop_signal_called and not(std::chrono::steady_clock::now() > stop_time))
    {
        // wait before sending next csd ref signal
        std::this_thread::sleep_for(std::chrono::milliseconds(csd_wait_time_millisec));
        uhd::time_spec_t tx_timer = usrp_classobj.usrp->get_time_now() + uhd::time_spec_t(0.1);

        if (usrp_classobj.transmission(buff, tx_timer))
        {
            std::cout << "ZFC transmission successful" << std::endl;
        }
        else
        {
            std::cerr << "ZFC ref signal transmission FAILED!" << std::endl;
            return EXIT_FAILURE;
        }

        uhd::time_spec_t last_sample_tx_time = tx_timer + uhd::time_spec_t(sample_duration * buff.size());
        uhd::time_spec_t rx_time = last_sample_tx_time + uhd::time_spec_t(tx_wait_time / 1e6 - (sample_duration * 5 * test_signal_len));

        auto rx_symbols = usrp_classobj.reception(num_rx_samps, rx_time);

        if (rx_symbols.size() < num_rx_samps)
        {
            std::cerr << "Reception failure!" << std::endl;
            return EXIT_FAILURE;
        }
        else
        {
            std::cout << "Successful reception in round " << iter_counter << std::endl
                      << std::endl;
        }

        // save received data into file
        std::string filename_it = filename + "_" + std::to_string(iter_counter) + ".dat";

        std::ofstream outfile;
        outfile.open(filename_it.c_str(), std::ofstream::binary);
        if (outfile.is_open())
        {
            outfile.write((const char *)&rx_symbols.front(), num_rx_samps * sizeof(std::complex<float>));
            outfile.close();
        }

        ++iter_counter;
    }

    return EXIT_SUCCESS;
}