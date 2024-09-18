#ifndef CALIBRATION
#define CALIBRATION

#include "pch.hpp"
#include "log_macros.hpp"
#include "usrp_class.hpp"
#include "config_parser.hpp"
#include "MQTTClient.hpp"
#include "cyclestartdetector.hpp"
#include "waveforms.hpp"

/**Calibration class to perform calibration between pair of leaf and cent nodes.
 *
 * Note that device serial and type (cent or leaf) are specified as "device_id" and "device_type".
 * Initialize the class with "initialize()" and then run "run()" to conduct calibration.
 *
 */
class Calibration
{
public:
    /** Class initialization
     *
     * @param USRP_class         object for USRP
     * @param ConfigParser       object for parsing configurations
     * @param device_id          USRP serial number
     * @param device_type        "cent" or "leaf"
     * @param signal_stop_called to manage clean exit of program via SIGINT
     */
    Calibration(USRP_class &usrp_obj, ConfigParser &parser, const std::string &device_id, const std::string &counterpart_id, const std::string &device_type, bool &signal_stop_called);

    ~Calibration();

    bool initialize();

    void run();
    void stop();

    bool signal_stop_called, calibration_successful, calibration_ends;

private:
    ConfigParser parser;
    std::unique_ptr<USRP_class> usrp_obj;
    std::unique_ptr<CycleStartDetector> csd_obj;
    std::unique_ptr<PeakDetectionClass> peak_det_obj;
    std::vector<std::complex<float>> ref_waveform, rand_waveform;

    void initialize_peak_det_obj();
    void initialize_csd_obj();
    void generate_waveform();
    void get_mqtt_topics();

    void transmit_waveform(const float &scale = 1.0);
    bool proximity_check(const float &val1, const float &val2);
    void callback_detect_flags(const std::string &payload);
    void callback_update_ltoc(const std::string &payload);

    std::atomic<bool> csd_success_flag;
    boost::thread producer_thread, consumer_thread;

    void consumer();

    /** Producer for the leaf node -- Detect REF, est. RSSI, and update TX gains.
     *
     * The leaf node listens for REF and at successful reception captures subsequent samples sent at full-scale.
     * Then, leaf node starts first by transmitting REF followed by full-scale signal.
     * The cent detects REF, est. Rx power, and send the value over MQTT to the leaf node.
     * Leaf node compares this value with Rx power est. of signal from the cent, and updates its TX gain to match at the cent.
     * This process is repeated until a reasonable accuracy is achieved.
     *
     */
    void producer_leaf();

    /** Producer for cent node -- Tx REF, Detect and Rx REF, send Rx pow value
     *
     * The cent node is a passive participant in the calibratoin process.
     * It updates the Rx power of the signal from leaf, and informs the leaf.
     *
     */
    void producer_cent();

    std::string device_id, counterpart_id, leaf_id, cent_id, device_type, client_id;
    std::string CFO_topic, flag_topic, full_scale_topic, ltoc_topic, tx_gain_topic, rx_gain_topic;
    size_t num_samps_sync;
    size_t subseq_tx_wait = 50, tx_rand_wait_microsec; // millisec
    size_t total_reps_cal = 10, current_reps_cal = 0;
    float max_tx_gain = 86.0, max_rx_gain = 70.0;

    bool recv_success = false;
    float ltoc, ctol, full_scale;
    bool recv_flag = false, retx_flag = false, end_flag = false;
};

#endif // CALIBRATION

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