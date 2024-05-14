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

    // USRP init
    USRP_class usrp_classobj(parser);
    usrp_classobj.initialize();

    // prepare to transmit ZFC seq
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t m_zfc = parser.getValue_int("Ref-m-zfc");
    size_t R_zfc = parser.getValue_int("Ref-R-zfc");

    auto zfc_seq = generateZadoffChuSequence(N_zfc, m_zfc);

    std::vector<std::complex<float>> buff(N_zfc * R_zfc);
    for (int i = 0; i < N_zfc * R_zfc; ++i)
    {
        buff[i] = zfc_seq[i % N_zfc];
    }

    uhd::time_spec_t tx_timer = usrp_classobj.usrp->get_time_now() + uhd::time_spec_t(0.1);
    bool tx_ref = usrp_classobj.transmission(buff, tx_timer);
    if (tx_ref)
    {
        std::cout << "ZFC transmission successful" << std::endl;
    }
    else
    {
        std::cerr << "ZFC ref signal transmission FAILED!" << std::endl;
        return EXIT_FAILURE;
    }

    float sample_duration = usrp_classobj.tx_sample_duration.get_real_secs();
    uhd::time_spec_t last_sample_tx_time = tx_timer + uhd::time_spec_t(sample_duration * (N_zfc * R_zfc));

    float tx_wait_time = parser.getValue_float("tx-wait-microsec");
    size_t test_signal_len = parser.getValue_int("test-signal-len");
    double add_rx_duration = tx_wait_time / 1e6 - (sample_duration * 5 * test_signal_len);
    uhd::time_spec_t rx_time = last_sample_tx_time + uhd::time_spec_t(add_rx_duration);
    size_t test_tx_reps = parser.getValue_int("test-tx-reps");
    size_t num_rx_samps = test_signal_len * (10 + test_tx_reps);

    auto rx_symbols = usrp_classobj.reception(num_rx_samps, rx_time);

    if (rx_symbols.size() < num_rx_samps)
    {
        std::cerr << "Reception failure!" << std::endl;
        return EXIT_FAILURE;
    }

    // save received data into file
    std::string filename = parser.getValue_str("test-file");
    if (filename == "NULL")
    {
        if (argc < 3)
        {
            filename = currentDir + "/OTA-C/cpp/storage/cent_test_def.dat";
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

    std::ofstream outfile;
    outfile.open(filename.c_str(), std::ofstream::binary);
    if (outfile.is_open())
    {
        outfile.write((const char *)&rx_symbols.front(), num_rx_samps * sizeof(std::complex<float>));
        outfile.close();
    }

    return EXIT_SUCCESS;
}