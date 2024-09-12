#ifndef SYNC_TEST
#define SYNC_TEST

#include "pch.hpp"
#include "log_macros.hpp"
#include "usrp_class.hpp"
#include "config_parser.hpp"
#include "MQTTClient.hpp"
#include "cyclestartdetector.hpp"
#include "waveforms.hpp"

class SyncTest
{
public:
    SyncTest(USRP_class &usrp_obj, ConfigParser &parser, bool &signal_stop_called);

    ~SyncTest();

    bool initialize();

    void run_sync();

    bool signal_stop_called;

private:
    USRP_class &usrp_obj;
    ConfigParser parser;
    std::unique_ptr<CycleStartDetector> csd_obj;
    std::unique_ptr<PeakDetectionClass> peak_det_obj;

    void initialize_peak_det_obj();
    void initialize_csd_obj();

    std::atomic<bool> stop_flag;
    boost::thread producer_thread, consumer_thread;

    void consumer_leaf(), producer_leaf(), consumer_cent(), producer_cent();
};

#endif // SYNC_TEST