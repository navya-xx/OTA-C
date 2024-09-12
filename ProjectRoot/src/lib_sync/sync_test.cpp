#include "sync_test.hpp"

SyncTest::SyncTest(USRP_class &usrp_obj_, ConfigParser &parser_, bool &signal_stop_called_) : usrp_obj(usrp_obj_), parser(parser_), signal_stop_called(signal_stop_called_), csd_obj(nullptr), peak_det_obj(nullptr), stop_flag(false) {};

void SyncTest::initialize_peak_det_obj()
{
    peak_det_obj = std::make_unique<PeakDetectionClass>(parser, usrp_obj.init_noise_ampl);
};

void SyncTest::initialize_csd_obj()
{
    size_t capacity = std::pow(2.0, parser.getValue_int("capacity-pow"));
    double rx_sample_duration_float = 1 / parser.getValue_float("rate");
    uhd::time_spec_t rx_sample_duration = uhd::time_spec_t(rx_sample_duration_float);
    csd_obj = std::make_unique<CycleStartDetector>(parser, capacity, rx_sample_duration, *peak_det_obj);
};

bool SyncTest::initialize()
{
    try
    {
        initialize_peak_det_obj();
        initialize_csd_obj();
        return true;
    }
    catch (std::exception e)
    {
        LOG_WARN_FMT("SyncTest Initialization failed with ERROR: %1%", e.what());
        return false;
    }
};

void SyncTest::run_sync()
{
    std::string device_type = parser.getValue_str("device-type");
    if (device_type == "leaf")
    {
        producer_thread = boost::thread(&SyncTest::producer_leaf, this);
        consumer_thread = boost::thread(&SyncTest::consumer_leaf, this);
    }
    else if (device_type == "leaf")
    {
        producer_thread = boost::thread(&SyncTest::producer_cent, this);
        consumer_thread = boost::thread(&SyncTest::consumer_cent, this);
    }
};

SyncTest::~SyncTest()
{
    stop_flag.store(true);

    if (producer_thread.joinable())
    {
        producer_thread.join();
    }
    if (consumer_thread.joinable())
    {
        consumer_thread.join();
    }
}

void SyncTest::producer_leaf()
{
    // reception/producer params
    size_t max_rx_packet_size = usrp_obj.max_rx_packet_size;
    size_t round = 1;
    std::vector<std::complex<float>> buff(max_rx_packet_size);
    std::string device_id = parser.getValue_str("device-id");
    std::string cent_id = parser.getValue_str("cent-id");
    size_t num_samps_sync = parser.getValue_int("Ref-N-zfc") * parser.getValue_int("Ref-R-zfc") * 10;
    float cent_tx_gain = 0.0, cent_rx_gain = 0.0;

    std::string client_id = device_id;
    MQTTClient &mqttClient = MQTTClient::getInstance(client_id);
    std::string CFO_topic = "calibration/CFO/" + device_id;
    std::string RSS_topic = "synctest/rss/" + device_id;
    auto format_float_data = [](float data) -> std::string
    {
        std::string text = "{'value':" + floatToStringWithPrecision(data, 8) + ", 'time': " + currentDateTime() + "}";
        return text;
    };

    // This function is called by the receiver as a callback everytime a frame is received
    auto producer_wrapper = [this](const std::vector<std::complex<float>> &samples, const size_t &sample_size, const uhd::time_spec_t &sample_time)
    {
        csd_obj->produce(samples, sample_size, sample_time, signal_stop_called);

        if (stop_flag)
            return true;
        else
            return false;
    };

    while (not signal_stop_called)
    {
        LOG_INFO_FMT("-------------- Round %1% ------------", round);

        usrp_obj.reception(signal_stop_called, 0, 0, uhd::time_spec_t(0.0), false, producer_wrapper);

        if (signal_stop_called)
            break;

        // publish CFO value
        mqttClient.publish(CFO_topic, format_float_data(csd_obj->cfo), true);

        // capture signal after a specific duration from the peak
        float wait_time = csd_obj->tx_wait_microsec;
        uhd::time_spec_t rx_timer = usrp_obj.usrp->get_time_now() + uhd::time_spec_t(wait_time);
        auto rx_samples = usrp_obj.reception(signal_stop_called, num_samps_sync, 0.0, rx_timer, true);

        // Leaf process rx_samples to obtain RSS value
        float rx_rssi = calc_signal_power(rx_samples, 0, 0, 0.05);
        mqttClient.publish(RSS_topic, format_float_data(rx_rssi), false);

        // get gains of cent dev
        std::atomic<bool> got_data(false);
        std::function<void(const std::string &)> cent_data_parser = [&cent_tx_gain, &cent_rx_gain, &got_data](const std::string &payload)
        {
            // Parse the JSON payload
            json json_data = json::parse(payload);
            cent_tx_gain = json_data["tx-gain"];
            cent_rx_gain = json_data["rx-gain"];
            got_data.store(true);
        };
        std::string cent_data_topic = "usrp/init_data/" + cent_id;
        mqttClient.setCallback(cent_data_topic, cent_data_parser);
        size_t max_timer = 10, timer = 0;
        while (got_data.load(std::memory_order::relaxed) == false && timer++ < max_timer)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // update Tx gain based on RSSI value
        // est_ch_strength =

        // move to next round
        stop_flag = false;

        // stop here (only one round for now)
        // stop_signal_called = true;
    }
};

void SyncTest::consumer_leaf()
{
    while (not signal_stop_called)
    {
        csd_obj->consume(stop_flag, signal_stop_called);
        if (stop_flag)
        {
            LOG_INFO("***Successful CSD!");
        }
    }
};