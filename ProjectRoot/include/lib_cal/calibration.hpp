#ifndef CALIBRATION
#define CALIBRATION

#include "pch.hpp"
#include "log_macros.hpp"
#include "usrp_class.hpp"
#include "config_parser.hpp"
#include "MQTTClient.hpp"
#include "cyclestartdetector.hpp"
#include "waveforms.hpp"

/**Calibration protocol implementation between pair of leaf and cent nodes. */
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

    void run_proto1();
    void run_proto2();
    void run_scaling_tests();
    void stop();

    bool signal_stop_called, calibration_successful, calibration_ends, scaling_test_ends;

private:
    ConfigParser parser;
    std::unique_ptr<USRP_class> usrp_obj;
    std::unique_ptr<CycleStartDetector> csd_obj;
    std::unique_ptr<PeakDetectionClass> peak_det_obj;
    std::vector<std::complex<float>> ref_waveform, otac_waveform;

    void initialize_peak_det_obj();
    void initialize_csd_obj();
    void generate_waveform();
    void get_mqtt_topics();

    bool calibrate_gains(MQTTClient &mqttClient);
    void on_calib_success(MQTTClient &mqttClient);
    void run_scaling_tests_cent();
    void run_scaling_tests_leaf();

    bool transmission_ref(const float &scale = 1.0, const uhd::time_spec_t &tx_timer = uhd::time_spec_t(0.0));
    bool transmission_otac(const float &scale = 1.0, const uhd::time_spec_t &tx_timer = uhd::time_spec_t(0.0));
    bool reception_ref(float &rx_sig_pow, uhd::time_spec_t &tx_timer);
    bool reception_otac(float &otac_sig_pow, uhd::time_spec_t &tx_timer);

    bool proximity_check(const float &val1, const float &val2);
    void callback_detect_flags(const std::string &payload);
    void callback_update_ltoc(const std::string &payload);
    bool check_ctol();

    std::atomic<bool> csd_success_flag;
    boost::thread producer_thread, consumer_thread;

    /** Leaf calibration protocol #1.
     * 1. Receive multiple REF signals from cent to get a stable estimate of channel power
     * 2. Transmit multiple REF signals until cent sends an MQTT message with received power
     * 3. Update gains to calibrate leaf such that ctol and ltoc rx signal powers are matched
     */
    void producer_leaf_proto1();
    void consumer_leaf_proto1();
    void producer_cent_proto1();
    void consumer_cent_proto1();

    /** Calibration protocol #2.
     * 1. Leaf receives REF signal and transmits a seq of random phase signals (say S) with specified delay and constant amplitude (after removing ctol channel power)
     * 2. Cent receives S and estimates mean square norm and timer, sends mean square norm value to leaf via mqtt
     * 3. Leaf adjusts gain/scale such that the estimated signal mean square value matched with the constant amplitude used by the leaf for Tx
     */
    void producer_leaf_proto2();
    void consumer_leaf_proto2();
    void producer_cent_proto2();
    void consumer_cent_proto2();

    std::string device_id, counterpart_id, leaf_id, cent_id, device_type, client_id;
    std::string CFO_topic, flag_topic_leaf, cal_scale_topic, full_scale_topic, ltoc_topic, ctol_topic, tx_gain_topic, rx_gain_topic, mctest_topic;
    size_t max_total_round = 20, max_mctest_rounds = 100, reps_total = 20;
    float max_tx_gain = 86.0, max_rx_gain = 50.0;

    bool recv_success = false;
    size_t total_reps_cal = 2, current_reps_cal = 0;
    float ltoc, ctol, full_scale = 1.0, calib_sig_scale = 0.8, min_sigpow_mul = 100, proximity_tol = 5e-2;
    bool recv_flag = false, retx_flag = false, end_flag = false;

    float min_e2e_pow = 1.0;

    std::vector<float> leaf_by_cent_ratios;
};

#endif // CALIBRATION