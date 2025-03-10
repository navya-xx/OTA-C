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
    if (argc < 3)
        throw std::invalid_argument("ERROR : filename with stream data missing.");

    std::string device_id = argv[1];
    parser.set_value("device-id", device_id, "str", "USRP device number");
    std::string filename = argv[2];
    filename = homeDirStr + "/OTA-C/cpp/storage/" + filename;

    // Logger
    // Logger logger(homeDirStr + "/OTA-C/cpp/logs/log_" + device_id + ".log", Logger::Level::DEBUG, true);

    parser.print_values();

    // USRP init
    USRP_class usrp_classobj(parser);
    usrp_classobj.initialize();

    // get symbols from file
    auto tx_waveform = read_complex_data_from_file(filename);

    // transmit waveform
    usrp_classobj.transmission(tx_waveform, uhd::time_spec_t(0.0), true);
    // usrp_classobj.transmission(tx_waveform_zfc, uhd::time_spec_t(0.0), true);
    // usrp_classobj.transmission(tx_waveform_imp, uhd::time_spec_t(0.0), true);

    return EXIT_SUCCESS;
}