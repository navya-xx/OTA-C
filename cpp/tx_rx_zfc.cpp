#include "utility_funcs.hpp"
#include "usrp_routines.hpp"

namespace po = boost::program_options;
using namespace std::chrono_literals;

static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

extern const bool DEBUG = true;

/***********************************************************************
 * Main code + dispatcher
 **********************************************************************/
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
        if (argc < 2)
            throw std::invalid_argument("ERROR : device address missing!");

        args = argv[1];
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

    if (DEBUG)
        std::cout << "USRP device setup -> Done!" << std::endl;

    // setup and create streamers
    uhd::tx_streamer::sptr tx_streamer;
    uhd::rx_streamer::sptr rx_streamer;

    auto streamers = create_usrp_streamers(usrp, parser);
    rx_streamer = streamers.first;
    tx_streamer = streamers.second;

    // ---------------------------------------------------------------------------------------
    // TX - send ZFC reference signal for Cycle start detection

    size_t Ref_N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t Ref_m_zfc = parser.getValue_int("Ref-m-zfc");
    size_t Ref_R_zfc = parser.getValue_int("Ref-R-zfc");

    // transmit REF signal
    uhd::time_spec_t tx_time = csd_tx_ref_signal(usrp, tx_streamer, Ref_N_zfc, Ref_m_zfc, Ref_R_zfc, uhd::time_spec_t(0.1), stop_signal_called);

    if (DEBUG)
        std::cout << "USRP Tx time = " << tx_time.get_real_secs() * 1e6 << std::endl;

    // compute time_stamp of beginning of last ref seq
    float sample_duration = 1 / usrp->get_tx_rate();
    // align to the first sample of the last sequence sent
    double sync_shift_duration = sample_duration * Ref_R_zfc * Ref_N_zfc;
    uhd::time_spec_t ref_time = tx_time + uhd::time_spec_t(sync_shift_duration);

    // ---------------------------------------------------------------------------------------
    // setup Rx streaming

    float tx_wait_time = parser.getValue_float("tx-wait-microsec");
    size_t test_signal_len = parser.getValue_int("test-signal-len");
    double add_rx_duration = tx_wait_time / 1e6 - (sample_duration * 5 * test_signal_len);
    uhd::time_spec_t rx_time = ref_time + uhd::time_spec_t(add_rx_duration);

    if (DEBUG)
        std::cout << "USRP Rx time = " << rx_time.get_real_secs() * 1e6 << std::endl;

    std::string test_outfile = parser.getValue_str("test-file");
    if (test_outfile == "NULL")
    {
        if (argc > 2)
            test_outfile = argv[2];
        else
        {
            std::cerr << "ERROR : Test output filename cannot be determined." << std::endl;
            return EXIT_FAILURE;
        }
    }

    csd_rx_test_signal(usrp, rx_streamer, test_signal_len, rx_time, test_signal_len * 10, test_outfile, stop_signal_called);

    return EXIT_SUCCESS;
}