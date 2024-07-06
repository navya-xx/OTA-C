#include "pch.hpp"
#include "Utility.hpp"
#include "ConfigParser.hpp"
#include "USRPclass.hpp"

namespace po = boost::program_options;

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    /*------ Initialize ---------------*/
    const char *homeDir = std::getenv("HOME");
    std::string homeDirStr(homeDir);
    std::string projectDir = homeDirStr + "/OTA-C/ProjectRoot";
    std::string curr_time_str = currentDateTimeFilename();

    if (argc < 2)
        throw std::invalid_argument("ERROR : device address missing! Pass it as first argument to the function call.");

    std::string device_id = argv[1];

    // Logger logger(projectDir + "/storage/logs/" + device_id + "_" + curr_time_str + ".log", Logger::Level::DEBUG, true);

    /*------ Parse Config ----------*/
    ConfigParser parser(projectDir + "/main/centralized_arch/config.conf");
    parser.set_value("device-id", device_id, "str", "USRP device number");

    if (argc > 2)
    {
        size_t rand_seed = std::stoi(argv[2]);
        parser.set_value("rand-seed", std::to_string(rand_seed), "int", "Random seed selected by the leaf node");
    }

    parser.print_values();

    /*------- USRP setup -----------*/
    USRP_class usrp_classobj(parser);

    usrp_classobj.initialize();

    return EXIT_SUCCESS;
};