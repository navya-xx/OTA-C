/** Standalone code to run centralized OTAC program at each node.
 *
 * Device type and serial are passed as arguments to the program.
 * Program uses MQTT to receive commands and send data to a controller (web application).
 */

#include "pch.hpp"

#include "usrp_class.hpp"
#include "config_parser.hpp"
#include "calibration.hpp"
#include "log_macros.hpp"
#include "utility.hpp"
#include "MQTTClient.hpp"

#define LOG_LEVEL LogLevel::DEBUG
static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    /*------ Initialize ---------------*/
    std::string homeDirStr = get_home_dir();
    std::string projectDir = homeDirStr + "/OTA-C/ProjectRoot";
    std::string curr_time_str = currentDateTimeFilename();

    if (argc < 3)
        throw std::invalid_argument("Insufficient arguments! Program requires 2 arguments - device_type (cent or leaf) and device_id (USRP serial)");

    std::string device_type = argv[1];
    std::string device_id = argv[2];

    /*------- LOG ------------------------*/
    std::string logFileName = projectDir + "/storage/logs/" + device_type + "_" + device_id + "_" + curr_time_str + ".log";
    Logger::getInstance().initialize(logFileName);
    Logger::getInstance().setLogLevel(LOG_LEVEL);

    /*------- Parse Config -------------*/
    ConfigParser parser(projectDir + "/config/config.conf");
    parser.set_value("device-id", device_id, "str", "USRP device serial");
    parser.set_value("storage-folder", projectDir + "/storage", "str", "Location of storage directory");

    /*------- MQTT Client setup -------*/
    MQTTClient &mqttClient = MQTTClient::getInstance(device_id);

    /*------- Obtain data ---------*/
    // get last CFO
    std::string val = "";
    std::string topic_cfo = mqttClient.topics->getValue_str("CFO") + device_id;
    mqttClient.temporary_listen_for_last_value(val, topic_cfo, 10, 100);
    if (val != "")
        parser.set_value("cfo", val, "float", "CFO-value from calibrated data");

    // get last tx-gain
    val = "";
    std::string topic_tx_gain = mqttClient.topics->getValue_str("tx-gain") + device_id;
    mqttClient.temporary_listen_for_last_value(val, topic_tx_gain, 10, 100);
    if (val != "")
        parser.set_value("tx-gain", val, "float", "Tx-value from calibrated data");

    // get last rx-gain
    val = "";
    std::string topic_rx_gain = mqttClient.topics->getValue_str("rx-gain") + device_id;
    mqttClient.temporary_listen_for_last_value(val, topic_rx_gain, 10, 100);
    if (val != "")
        parser.set_value("rx-gain", val, "float", "Rx-value from calibrated data");

    /*------- USRP setup --------------*/
    USRP_class usrp_obj(parser);

    // external reference
    usrp_obj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_obj.initialize();

    parser.set_value("max-rx-packet-size", std::to_string(usrp_obj.max_rx_packet_size), "int", "Max Rx packet size");

    parser.print_values();

    /*-------- Subscribe to Control topics ---------*/
    // Calibration
    Calibration calib_class_obj(usrp_obj, parser, device_id, device_type, stop_signal_called);
    if (!calib_class_obj.initialize())
    {
        LOG_WARN("Calibration Class object initilization FAILED!");
    }

    auto control_calibration_callback = [&calib_class_obj](const std::string &payload)
    {
        json jdata;
        try
        {
            jdata = json::parse(payload);
        }
        catch (json::exception &e)
        {
            LOG_WARN_FMT("JSON error : %1%", e.what());
            LOG_WARN_FMT("Incorrect format of control message = %1%", payload);
            return;
        }
        std::string msg = jdata["value"];
        if (msg == "start")
        {
            LOG_INFO("Starting Calibration routine...");
            calib_class_obj.run();
        }
        else if (msg == "stop")
        {
            LOG_INFO("Stopping Calibration routine...");
            calib_class_obj.stop();
        }
    };

    auto control_calib_topic = mqttClient.topics->getValue_str("calibration") + device_id;
    mqttClient.setCallback(control_calib_topic, control_calibration_callback);

    while (not stop_signal_called)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return EXIT_SUCCESS;
};