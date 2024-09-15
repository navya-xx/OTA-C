#ifndef SYNC_TEST
#define SYNC_TEST

#include "pch.hpp"
#include <boost/chrono.hpp>
#include "log_macros.hpp"
#include "usrp_class.hpp"
#include "config_parser.hpp"
#include "MQTTClient.hpp"
#include "cyclestartdetector.hpp"
#include "waveforms.hpp"

class Calibration
{
public:
    Calibration(USRP_class &usrp_obj, ConfigParser &parser, bool &signal_stop_called);

    ~Calibration();

    bool initialize();

    void run_sync();

    bool signal_stop_called;

private:
    USRP_class &usrp_obj;
    ConfigParser parser;
    std::unique_ptr<CycleStartDetector> csd_obj;
    std::unique_ptr<PeakDetectionClass> peak_det_obj;
    std::vector<std::complex<float>> ref_waveform, rand_waveform;

    void initialize_peak_det_obj();
    void initialize_csd_obj();
    void generate_waveform();

    std::atomic<bool> stop_flag;
    boost::thread producer_thread, consumer_thread;

    void consumer(), producer_leaf(), producer_cent();

    std::string leaf_id, cent_id;
    std::string CFO_topic, flag_topic, ctol_rxpow_topic, ltoc_rxpow_topic, calibrated_tx_gain_topic, calibrated_rx_gain_topic;
    size_t num_samps_sync;
    size_t subseq_tx_wait = 100, tx_rand_wait; // millisec
    float max_tx_gain = 86.0, max_rx_gain = 70.0;
};

#endif // SYNC_TEST

// get gains of cent dev
// std::atomic<bool> got_data(false);
// std::function<void(const std::string &)> cent_data_parser = [&cent_tx_gain, &cent_rx_gain, &got_data](const std::string &payload)
// {
//     // Parse the JSON payload
//     json json_data = json::parse(payload);
//     cent_tx_gain = json_data["tx-gain"];
//     cent_rx_gain = json_data["rx-gain"];
//     got_data.store(true);
// };
// std::string cent_data_topic = "usrp/init_data/" + cent_id;
// mqttClient.setCallback(cent_data_topic, cent_data_parser);
// size_t max_timer = 10, timer = 0;
// while (got_data.load(std::memory_order::relaxed) == false && timer++ < max_timer)
//     std::this_thread::sleep_for(std::chrono::milliseconds(20));