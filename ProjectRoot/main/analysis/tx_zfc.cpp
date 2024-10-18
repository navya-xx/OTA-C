#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"
#include "usrp_class.hpp"
#include "waveforms.hpp"

#define LOG_LEVEL LogLevel::DEBUG
static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

int random_delay(const int &min, const int &max)
{
    std::random_device rd;                           // Seed generator
    std::mt19937 gen(rd());                          // Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<> distr(min, max); // Range from 1e5 to 5e5 microsec

    // Generate a random delay
    int delay = distr(gen);

    return delay; // microsecs
}

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    /*------ Initialize ---------------*/
    std::string homeDirStr = get_home_dir();
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
    ConfigParser parser(projectDir + "/config/config.conf");
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
    float tx_samp_rate = usrp_classobj.tx_rate;

    /*------- Generate Waveform -------*/
    WaveformGenerator wf_gen = WaveformGenerator();
    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t q_zfc = parser.getValue_int("Ref-m-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    size_t sampling_factor = parser.getValue_int("sampling-factor");
    wf_gen.initialize(wf_gen.ZFC, N_zfc, reps_zfc, 0, q_zfc, 1.0, 0, false);
    std::vector<std::complex<float>> ref_waveform = wf_gen.generate_waveform();

    for (int i = 0; i < 4; i++)
    {
        ref_waveform.insert(ref_waveform.end(), ref_waveform.begin(), ref_waveform.end());
    }

    auto tx_waveform = upsample(ref_waveform, sampling_factor);

    /*-------- Transmit Waveform --------*/
    // transmit multiple copies of the waveform with random delays
    // std::vector<std::complex<float>> complete_tx_seq;

    // size_t random_min = int(1e4), random_max = int(1e5);
    int wait_duration = 2;
    LOG_INFO_FMT("Starting transmission in %1% secs.", wait_duration);
    std::this_thread::sleep_for(std::chrono::milliseconds(wait_duration * 1000));

    usrp_classobj.transmission(ref_waveform, usrp_classobj.usrp->get_time_now() + double(0.01), stop_signal_called, true);

    // for (int i = 0; i < 100; i++)
    // {
    //     // int delay = random_delay(random_min, random_max);
    //     uhd::time_spec_t wait_duration = uhd::time_spec_t(double(1e5 / tx_samp_rate));
    //     usrp_classobj.transmission(tx_waveform, usrp_classobj.usrp->get_time_now() + wait_duration, stop_signal_called, true);
    //     std::this_thread::sleep_for(std::chrono::milliseconds(1));
    // }

    return EXIT_SUCCESS;
};