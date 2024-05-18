#include "USRP_class.hpp"
#include "utility_funcs.hpp"
#include "ConfigParser.hpp"
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

    if (argc < 2)
        throw std::invalid_argument("ERROR : device address missing! Pass it as first argument to the function call.");

    std::string device_id = argv[1];
    parser.set_value("device-id", device_id, "str", "USRP device number");

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
            filename = currentDir + "/OTA-C/cpp/storage/" + argv[2];
    }
    else
        filename = currentDir + "/OTA-C/cpp/storage/" + filename;

    // USRP init
    USRP_class usrp_classobj(parser);
    usrp_classobj.initialize();

    // prepare to transmit ZFC seq
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t m_zfc = parser.getValue_int("Ref-m-zfc");
    size_t R_zfc = parser.getValue_int("Ref-R-zfc");

    auto zfc_seq = generateZadoffChuSequence(N_zfc, m_zfc);

    // pre- and post- append buff with some random signal
    auto app_buff = generateUnitCircleRandom(2 * N_zfc, 1.0);

    std::vector<std::complex<float>> buff(N_zfc * R_zfc, std::complex<float>(0.0, 0.0));
    for (int i = 0; i < N_zfc * R_zfc; ++i)
    {
        buff[i] = zfc_seq[i % N_zfc];
    }

    buff.insert(buff.begin(), app_buff.begin(), app_buff.end());
    buff.insert(buff.end(), app_buff.begin(), app_buff.end());

    float total_runtime = parser.getValue_float("duration");
    if (total_runtime == 0.0)
        total_runtime = 5 * 60; // run for 5 minutes
    const auto start_time = std::chrono::steady_clock::now();
    const auto stop_time = start_time + std::chrono::milliseconds(int64_t(1000 * total_runtime));

    size_t csd_wait_time_millisec = parser.getValue_int("csd-wait-time-millisec");
    float tx_wait_time = parser.getValue_float("tx-wait-microsec") / 1e6;
    size_t test_signal_len = parser.getValue_int("test-signal-len");
    float sample_duration = usrp_classobj.tx_sample_duration.get_real_secs();
    size_t test_tx_reps = parser.getValue_int("test-tx-reps");
    size_t sync_with_peak_from_last = parser.getValue_int("sync-with-peak-from-last");
    // float tx_reps_gap = parser.getValue_int("tx-gap-millisec") / 1e3;

    size_t save_extra_mul = 2;
    // save data from tx_time - save_extra_mul * test_signal_duration to end of test signal + save_extra_mul * test_signal_duration
    size_t num_rx_samps = test_tx_reps * test_signal_len + 2 * save_extra_mul * test_signal_len;

    int iter_counter = 1;

    while (not stop_signal_called and not(std::chrono::steady_clock::now() > stop_time))
    {
        // wait before sending next csd ref signal
        uhd::time_spec_t tx_timer = usrp_classobj.usrp->get_time_now() + uhd::time_spec_t(csd_wait_time_millisec / 1e3);

        if (usrp_classobj.transmission(buff, tx_timer))
        {
            std::cout << "ZFC transmission successful" << std::endl;
        }
        else
        {
            std::cerr << "ZFC ref signal transmission FAILED!" << std::endl;
            return EXIT_FAILURE;
        }

        uhd::time_spec_t rx_time = tx_timer + uhd::time_spec_t(sample_duration * (N_zfc * (R_zfc - sync_with_peak_from_last) + app_buff.size())) + uhd::time_spec_t(tx_wait_time - (sample_duration * save_extra_mul * test_signal_len));

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