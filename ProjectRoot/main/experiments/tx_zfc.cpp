#include "pch.hpp"

#include "log_macros.hpp"
#include "Utility.hpp"
#include "ConfigParser.hpp"
#include "USRPclass.hpp"
#include "Waveforms.hpp"

#define LOG_LEVEL LogLevel::DEBUG
static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

void random_delay()
{
    std::random_device rd;                           // Seed generator
    std::mt19937 gen(rd());                          // Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<> distr(100, 500); // Range from 100 to 500 milliseconds

    // Generate a random delay
    int random_delay = distr(gen);

    // Sleep for the random delay
    std::this_thread::sleep_for(std::chrono::milliseconds(random_delay));
}

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

    /*----- LOG ------------------------*/
    std::string logFileName = projectDir + "/storage/logs/leaf_" + device_id + "_" + curr_time_str + ".log";
    Logger::getInstance().initialize(logFileName);
    Logger::getInstance().setLogLevel(LOG_LEVEL);

    /*------ Parse Config -------------*/
    ConfigParser parser(projectDir + "/main/centralized_arch/config.conf");
    parser.set_value("device-id", device_id, "str", "USRP device number");
    if (argc > 2)
    {
        size_t rand_seed = std::stoi(argv[2]);
        parser.set_value("rand-seed", std::to_string(rand_seed), "int", "Random seed selected by the leaf node");
    }
    parser.print_values();

    /*------- USRP setup --------------*/
    USRP_class usrp_classobj(parser);

    usrp_classobj.initialize();

    /*------- Generate Waveform -------*/
    WaveformGenerator wf_gen = WaveformGenerator();
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, q_zfc, 1.0, 0, false);
    auto tx_waveform = wf_gen.generate_waveform();

    /*-------- Transmit Waveform --------*/
    // transmit multiple copies of the waveform with random delays
    for (int i = 0; i < 20; i++)
    {
        usrp_classobj.transmission(tx_waveform, uhd::time_spec_t(0.0), stop_signal_called, true);
        random_delay();
    }

    return EXIT_SUCCESS;
};