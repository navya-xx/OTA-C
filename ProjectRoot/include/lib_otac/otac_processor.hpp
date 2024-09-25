#ifndef OTAC_PROCESSOR
#define OTAC_PROCESSOR

#include "pch.hpp"
#include "log_macros.hpp"
#include "usrp_class.hpp"
#include "config_parser.hpp"
#include "MQTTClient.hpp"
#include "cyclestartdetector.hpp"
#include "waveforms.hpp"

class OTAC_class
{
public:
    OTAC_class(USRP_class &usrp_obj, ConfigParser &parser, const std::string &device_id, const std::string &device_type, const float &dmin, const float &dmax, const size_t &num_leafs, bool &signal_stop_called);
    ~OTAC_class();

    bool initialize();

    void run_proto();
    void stop();

    bool signal_stop_called, otac_routine_ends = false;
    float otac_input, dmin, dmax, num_leafs;

private:
    ConfigParser parser;
    std::shared_ptr<USRP_class> usrp_obj;
    std::unique_ptr<CycleStartDetector> csd_obj;
    std::unique_ptr<PeakDetectionClass> peak_det_obj;
    std::vector<std::complex<float>> ref_waveform, otac_waveform;

    void initialize_peak_det_obj();
    void initialize_csd_obj();
    void generate_waveform();
    void get_mqtt_topics();

    bool otac_pre_processing(float &sig_scale);
    bool otac_post_processing(const float &mean_norm_val, float &out_scale);

    bool transmission_ref(const float &scale = 1.0, const uhd::time_spec_t &tx_timer = uhd::time_spec_t(0.0));
    bool transmission_otac(const float &scale = 1.0, const uhd::time_spec_t &tx_timer = uhd::time_spec_t(0.0));
    bool reception_ref(float &rx_sig_pow, uhd::time_spec_t &tx_timer);
    bool reception_otac(float &otac_sig_pow, uhd::time_spec_t &tx_timer);

    float compute_nmse(const float &val1, const float &val2);
    void callback_detect_flags(const std::string &payload);

    bool check_ctol();

    std::atomic<bool> csd_success_flag;
    boost::thread producer_thread, consumer_thread;

    /** OTAC protocol.
     * 1. Cent transmits ref -- Leafs detects, estimates channel power, and adjusts CFO and RX-gain.
     * 2. Leaf transmits pre-processed (modulate amplitude with f_n(x_n)) OTAC waveform at specified time
     * 3. Cent receives superimposed OTAC waveform and estimates sum (\sum_n f_n(x_n))
     * 4. Process is repeated multiple times to get a histogram of errors
     */
    void producer_leaf_proto();
    void consumer_leaf_proto();
    void producer_cent_proto();
    void consumer_cent_proto();

    std::string device_id, device_type;
    std::string tele_otac_topic;
    size_t max_total_round = 30;
    float max_rx_gain = 50.0, min_rx_gain = 20.0;
    float full_scale = 1.0, ctol = 0.0, ltoc = 0.0;
    float noise_power;
    std::vector<float> otac_output_list;

    float init_proximity_tol = 0.04, proximity_tol = 0.01;
    float min_e2e_pow = 1.0, max_e2e_pow = 1.0;
};

#endif // OTAC_PROCESSOR