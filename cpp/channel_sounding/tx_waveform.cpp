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
    size_t wf_len = parser.getValue_int("Ref-N-zfc");
    size_t zfc_q = parser.getValue_int("Ref-m-zfc");
    size_t wf_gap = 5 * wf_len;
    size_t counter = 0;

    WaveformGenerator wf_gen;

    std::vector<std::complex<float>> tx_waveform(wf_len * 40);
    std::vector<std::complex<float>> tmp_wf;

    tmp_wf = wf_gen.generate_waveform(wf_gen.ZFC, wf_len, 10, 0, zfc_q, 1.0, 123, true);

    for (auto &elem : tmp_wf)
    {
        tx_waveform[counter] = elem;
        ++counter;
    }

    tmp_wf.clear();

    // tx_waveform.insert(tx_waveform.begin(), tmp_wf.begin(), tmp_wf.end());
    std::cout << "ZFC seq len = " << tmp_wf.size() << std::endl;

    // tx_waveform.insert(tx_waveform.end(), wf_gap, std::complex<float>(0.0, 0.0));

    for (size_t i = 0; i < wf_gap; ++i)
    {
        tx_waveform[counter] = std::complex<float>(0.1, 0.1);
        ++counter;
    }

    tmp_wf = wf_gen.generate_waveform(wf_gen.IMPULSE, wf_len, 10, wf_len, 1, 1.0, 123, false);
    // tx_waveform.insert(tx_waveform.end(), tmp_wf.begin(), tmp_wf.end());
    for (auto &elem : tmp_wf)
    {
        tx_waveform[counter] = elem;
        ++counter;
    }
    std::cout << "IMPULSE seq len = " << tmp_wf.size() << std::endl;

    std::cout << "Total seq len = " << tx_waveform.size() << std::endl;

    // transmit waveform
    usrp_classobj.transmission(tx_waveform, uhd::time_spec_t(0.0), true);
    // usrp_classobj.transmission(tx_waveform_zfc, uhd::time_spec_t(0.0), true);
    // usrp_classobj.transmission(tx_waveform_imp, uhd::time_spec_t(0.0), true);

    return EXIT_SUCCESS;
}