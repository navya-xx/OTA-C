#include "CycleStartDetector.hpp"
#include "utility_funcs.hpp"
#include "usrp_routines.hpp"

/***************************************************************
 * Copyright (c) 2023 Navneet Agrawal
 *
 * Author: Navneet Agrawal
 * Email: navneet.agrawal@hhi.fraunhofer.de
 *
 * This code is licensed under the MIT License.
 *
 * For more information, see https://opensource.org/licenses/MIT
 **************************************************************/

namespace po = boost::program_options;
using namespace std::chrono_literals;

int UHD_SAFE_MAIN(int argc, char *argv[])
{

    std::string args;
    // setup the program options
    po::options_description desc("Configurations at 'leaf_config.conf'.");
    // clang-format off
    desc.add_options()
        ("help", "Program to start leaf-node processing.")
        ("args", po::value<std::string>(&args)->default_value("serial=32C79BE"), "multi uhd device address args")
    ;
    // clang-format on
    po::variables_map vmh, config;
    po::store(po::parse_command_line(argc, argv, desc), vmh);
    po::notify(vmh);

    // print the help message
    if (vmh.count("help"))
    {
        std::cout << boost::format("Over-the-air Function Computation (OTA-C). \n %s") % desc << std::endl;
        std::cout << std::endl
                  << "This application implements a leaf-node using a USRP. \n"
                     "Details will be made available in the future.\n"
                     "Configuration file is 'leaf_config.conf'.\n"
                  << std::endl;
        return ~0;
    }

    // USRP init
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    // rx and tx streamers -- initilize
    config = parse_config_from_file("../leaf_config.conf");

    // setup and create streamers
    uhd::tx_streamer::sptr tx_streamer;
    uhd::rx_streamer::sptr rx_streamer;

    auto streamers = create_usrp_streamers(usrp, config);
    rx_streamer = streamers.first;
    tx_streamer = streamers.second;

    // create data stream type based on config
    // -- we skip this for now

    // get initial noise level
    float init_noise_level = get_background_noise_level<std::complex<float>>(usrp, rx_streamer);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // protocol config
    bool keep_running = true;

    // Cycle Start Detector config
    size_t capacity_mul = config["capacity_mul"].as<size_t>() < 1 ? 1 : config["capacity_mul"].as<size_t>();
    size_t num_samp_corr = config["num_samp_corr"].as<size_t>();
    size_t max_sample_size = rx_streamer->get_max_num_samps();
    size_t capacity = capacity_mul * std::max(num_samp_corr, max_sample_size);
    float sample_duration = usrp->get_rx_rate(config["channel"].as<int>()) / 1e6;
    size_t Ref_N_zfc = config["Ref-N-zfc"].as<size_t>();
    size_t Ref_m_zfc = config["Ref-m-zfc"].as<size_t>();
    size_t Ref_R_zfc = config["Ref-R-zfc"].as<size_t>();
    float pnr_threshold = config["pnr-threshold"].as<float>();

    CycleStartDetector<std::complex<float>> csdbuffer(capacity, sample_duration, num_samp_corr, Ref_N_zfc, Ref_m_zfc, Ref_R_zfc, init_noise_level, pnr_threshold);

    if (config.count("file"))
        csdbuffer.save_buffer_flag = true;

    while (keep_running)
    {
    }

    return 0;
}