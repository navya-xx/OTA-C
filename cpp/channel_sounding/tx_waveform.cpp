#include "../USRP_class.hpp"
#include "../utility_funcs.hpp"
#include "../ConfigParser.hpp"
#include "../waveforms.hpp"
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <uhd/convert.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/date_time.hpp>
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
    std::string homeDirStr(homeDir);

    // rx and tx streamers -- initilize
    ConfigParser parser(homeDirStr + "/OTA-C/cpp/leaf_config.conf");

    if (argc < 2)
        throw std::invalid_argument("ERROR : device address missing! Pass it as first argument to the function call.");

    std::string device_id = argv[1];
    parser.set_value("device-id", device_id, "str", "USRP device number");

    // Logger
    // Logger logger(homeDirStr + "/OTA-C/cpp/logs/log_" + device_id + ".log", Logger::Level::DEBUG, true);

    parser.print_values();

    // USRP init
    USRP_class usrp_classobj(parser);
    usrp_classobj.initialize();

    // waveform selection
    size_t wf_len = 101;
    size_t zfc_q = 31;
    size_t wf_reps = 50;
    size_t wf_gap = 0;

    WaveformGenerator wf_gen;

    std::vector<std::complex<float>> tx_waveform;
    std::vector<std::complex<float>> tmp_wf;

    tmp_wf = wf_gen.generate_waveform(wf_gen.ZFC, wf_len, wf_reps, wf_gap, zfc_q, 1.0, 123, false);
    tx_waveform.insert(tx_waveform.end(), tmp_wf.begin(), tmp_wf.end());
    std::cout << "ZFC seq len = " << tmp_wf.size() << std::endl;

    size_t signal_gap = 100 * wf_len;
    tx_waveform.insert(tx_waveform.end(), signal_gap, std::complex<float>(0.0, 0.0));

    // tmp_wf = wf_gen.generate_waveform(wf_gen.IMPULSE, wf_len, 10, wf_len, 1, 1.0, 123, false);
    // tx_waveform.insert(tx_waveform.end(), tmp_wf.begin(), tmp_wf.end());
    // std::cout << "IMPULSE seq len = " << tmp_wf.size() << std::endl;

    tx_waveform.insert(tx_waveform.end(), tmp_wf.begin(), tmp_wf.end());

    std::cout << "Total seq len = " << tx_waveform.size() << std::endl;

    std::string filename = homeDirStr + "/OTA-C/cpp/storage/tx_" + device_id + ".dat";
    save_complex_data_to_file(filename, tx_waveform);

    // transmit waveform
    usrp_classobj.transmission(tx_waveform, uhd::time_spec_t(0.0), true);
    // usrp_classobj.transmission(tx_waveform_zfc, uhd::time_spec_t(0.0), true);
    // usrp_classobj.transmission(tx_waveform_imp, uhd::time_spec_t(0.0), true);

    return EXIT_SUCCESS;
}