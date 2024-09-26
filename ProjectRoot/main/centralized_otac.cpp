#include "pch.hpp"

#include "usrp_class.hpp"
#include "config_parser.hpp"
#include "calibration.hpp"
#include "otac_processor.hpp"
#include "log_macros.hpp"
#include "utility.hpp"
#include "MQTTClient.hpp"

#define LOG_LEVEL LogLevel::DEBUG
static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

void gen_mqtt_control_msg(std::string &device_id, std::string &counterpard_id, const bool &is_cent = false)
{
    int choice;

    // Present options to the user
    LOG_INFO("Choose from the following options:");
    LOG_INFO("(1) Calibrate a leaf device.");
    LOG_INFO("(2) Run scaling tests.");
    LOG_INFO("(3) Analyse time synchronization performance.");
    LOG_INFO("(4) Analyse OTAC-based consensus performance.");
    LOG_INFO("(5) Exit program.");
    LOG_INFO("Enter choice (1-5):");

    // Take input from the user
    std::cin >> choice;

    // Run code based on user input
    switch (choice)
    {
    case 1:
    {
        MQTTClient &mqttClient = MQTTClient::getInstance(device_id);
        std::string cent_id, leaf_id;
        if (is_cent)
        {
            cent_id = device_id;
            LOG_INFO("Enter serial of leaf device:");
            std::cin >> leaf_id;
            counterpard_id = leaf_id;
        }
        else
        {
            cent_id = counterpard_id;
            leaf_id = device_id;
        }
        std::string topic_calib = mqttClient.topics->getValue_str("calibration");
        json jstring;
        jstring["message"] = "start";
        jstring["leaf-id"] = leaf_id;
        jstring["cent-id"] = cent_id;
        jstring["time"] = currentDateTime();
        LOG_INFO_FMT("Sending data to topic %1% : %2%", topic_calib + cent_id, jstring.dump(4));
        mqttClient.publish(topic_calib + cent_id, jstring.dump(4), false);
        LOG_INFO_FMT("Sending data to topic %1% : %2%", topic_calib + leaf_id, jstring.dump(4));
        mqttClient.publish(topic_calib + leaf_id, jstring.dump(4), false);
        break;
    }
    case 2:
    {
        MQTTClient &mqttClient = MQTTClient::getInstance(device_id);
        std::string cent_id, leaf_id;
        if (is_cent)
        {
            cent_id = device_id;
            LOG_INFO("Enter serial of leaf device:");
            std::cin >> leaf_id;
            counterpard_id = leaf_id;
        }
        else
        {
            cent_id = counterpard_id;
            leaf_id = device_id;
        }
        std::string topic_scaling = mqttClient.topics->getValue_str("scaling-tests");
        json jstring;
        jstring["message"] = "start";
        jstring["leaf-id"] = leaf_id;
        jstring["cent-id"] = cent_id;
        jstring["time"] = currentDateTime();
        LOG_INFO_FMT("Sending data to topic %1% : %2%", topic_scaling + cent_id, jstring.dump(4));
        mqttClient.publish(topic_scaling + cent_id, jstring.dump(4), false);
        LOG_INFO_FMT("Sending data to topic %1% : %2%", topic_scaling + leaf_id, jstring.dump(4));
        mqttClient.publish(topic_scaling + leaf_id, jstring.dump(4), false);
        break;
    }
    case 3:
    {
        LOG_INFO("Not implemented yet!");
        break;
    }
    case 4:
    {
        std::vector<std::string> device_id_list;
        if (not listActiveDevices(device_id_list))
        {
            LOG_WARN("Unable to get device list.");
            break;
        }

        MQTTClient &mqttClient = MQTTClient::getInstance(device_id);

        float dmin = 1.0, dmax = 10.0;
        size_t num_leafs = device_id_list.size();

        std::string topic_otac = mqttClient.topics->getValue_str("otac");
        json jstring;
        jstring["message"] = "start";
        jstring["time"] = currentDateTime();
        jstring["dmin"] = dmin;
        jstring["dmax"] = dmax;
        jstring["num_leafs"] = num_leafs;

        // leafs
        float otac_input_sum = 0.0;
        for (const auto &dev_id : device_id_list)
        {
            // float otac_input_ = generateRandomFloat(dmin, dmax);
            float otac_input_ = dmax;
            otac_input_sum += otac_input_;
            jstring["otac_input"] = otac_input_;
            mqttClient.publish(topic_otac + dev_id, jstring.dump(4), false);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // give leafs some lead

        // cent
        jstring["otac_input"] = otac_input_sum;
        mqttClient.publish(topic_otac + device_id, jstring.dump(4), false);

        break;
    }
    case 5:
    {
        stop_signal_called = true;
        break;
    }
    default:
    {
        LOG_INFO("Invalid choice. Please enter a number between 1 and 5.");
        break;
    }
    }
}

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    /*------ Initialize ---------------*/
    std::string homeDirStr = get_home_dir();
    std::string projectDir = homeDirStr + "/OTA-C/ProjectRoot";
    std::string curr_time_str = currentDateTimeFilename();

    if (argc < 3)
        throw std::invalid_argument("Insufficient arguments! Program requires 3 arguments - device_type (cent or leaf) and device_id (USRP serial)");

    std::string device_type = argv[1];
    std::string device_id = argv[2];

    /*------- LOG ------------------------*/
    std::string logFileName = projectDir + "/storage/logs/" + device_type + "_" + device_id + "_" + curr_time_str + ".log";
    Logger::getInstance().initialize(logFileName);
    Logger::getInstance().setLogLevel(LOG_LEVEL);

    /*------- Parse Config -------------*/
    std::shared_ptr<ConfigParser> parser = std::make_shared<ConfigParser>(projectDir + "/config/config.conf");
    parser->set_value("device-id", device_id, "str", "USRP device serial");
    parser->set_value("storage-folder", projectDir + "/storage", "str", "Location of storage directory");

    /*------- MQTT Client setup -------*/
    MQTTClient &mqttClient = MQTTClient::getInstance(device_id);

    /*------- USRP setup --------------*/
    // USRP_class usrp_obj(parser);
    std::shared_ptr<USRP_class> usrp_obj = std::make_shared<USRP_class>(*parser);

    // external reference
    usrp_obj->external_ref = parser->getValue_str("external-clock-ref") == "true" ? true : false;

    parser->print_values();

    /*-------- Subscribe to Control topics ---------*/
    std::atomic_bool program_ends(true);

    // Calibration routine
    auto control_calibration_callback = [usrp_obj, parser, &program_ends, &device_type](const std::string &payload)
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
            program_ends.store(true);
            return;
        }

        std::string msg = jdata["message"];
        std::string main_dev, c_dev;
        if (device_type == "cent")
        {
            main_dev = jdata["cent-id"];
            c_dev = jdata["leaf-id"];
        }
        else if (device_type == "leaf")
        {
            main_dev = jdata["leaf-id"];
            c_dev = jdata["cent-id"];
        }

        if (device_type == "leaf")
            usrp_obj->use_calib_gains = false;
        usrp_obj->initialize();

        parser->set_value("max-rx-packet-size", std::to_string(usrp_obj->max_rx_packet_size), "int", "Max Rx packet size");

        // run background noise estimator
        // if (device_type == "leaf")
        //     usrp_obj->collect_background_noise_powers();

        Calibration calib_class_obj(*usrp_obj, *parser, main_dev, c_dev, device_type, stop_signal_called);

        if (!calib_class_obj.initialize())
        {
            LOG_WARN("Calibration Class object initilization FAILED!");
            program_ends.store(true);
            return;
        }

        if (msg == "start")
        {
            LOG_INFO("Starting Calibration routine...");
            calib_class_obj.run_proto2();
        }
        else if (msg == "stop")
        {
            LOG_INFO("Stopping Calibration routine...");
            calib_class_obj.stop();
        }

        while (!calib_class_obj.calibration_ends)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        LOG_INFO("Calbration ended.");

        program_ends.store(true);

        return;
    };
    auto control_calib_topic = mqttClient.topics->getValue_str("calibration") + device_id;
    mqttClient.setCallback(control_calib_topic, control_calibration_callback, true);

    // scaling test routine
    auto control_scaling_test_callback = [usrp_obj, parser, &program_ends, &device_type](const std::string &payload)
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
            program_ends.store(true);
            return;
        }

        std::string msg = jdata["message"];
        std::string main_dev, c_dev;
        if (device_type == "cent")
        {
            main_dev = jdata["cent-id"];
            c_dev = jdata["leaf-id"];
        }
        else if (device_type == "leaf")
        {
            main_dev = jdata["leaf-id"];
            c_dev = jdata["cent-id"];
        }

        if (device_type == "leaf")
            usrp_obj->use_calib_gains = true;
        usrp_obj->initialize();

        // run background noise estimator
        // if (device_type == "leaf")
        //     usrp_obj->collect_background_noise_powers();

        Calibration calib_class_obj(*usrp_obj, *parser, main_dev, c_dev, device_type, stop_signal_called);

        if (!calib_class_obj.initialize())
        {
            LOG_WARN("Calibration Class object initilization FAILED!");
            program_ends.store(true);
            return;
        }

        if (msg == "start")
        {
            LOG_INFO("Starting Calibration routine...");
            calib_class_obj.run_scaling_tests();
        }
        else if (msg == "stop")
        {
            LOG_INFO("Stopping Calibration routine...");
            calib_class_obj.stop();
        }

        while (!calib_class_obj.scaling_test_ends)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        LOG_INFO("Calbration ended.");

        program_ends.store(true);

        return;
    };
    auto control_scale_topic = mqttClient.topics->getValue_str("scaling-tests") + device_id;
    mqttClient.setCallback(control_scale_topic, control_scaling_test_callback, true);

    // TODO: Synchronization routine

    // OTAC routine
    auto control_otac_callback = [usrp_obj, parser, &program_ends, &device_id, &device_type](const std::string &payload)
    {
        LOG_INFO("------- Starting OTAC routine ----------- ");
        json jdata;
        try
        {
            jdata = json::parse(payload);
        }
        catch (json::exception &e)
        {
            LOG_WARN_FMT("JSON error : %1%", e.what());
            LOG_WARN_FMT("Incorrect format of control message = %1%", payload);
            program_ends.store(true);
            return;
        }

        std::string msg = jdata["message"];
        float input_data = jdata["otac_input"];
        float dmin = jdata["dmin"];
        float dmax = jdata["dmax"];
        size_t num_leafs = jdata["num_leafs"];

        if (device_type == "leaf")
            usrp_obj->use_calib_gains = true;
        usrp_obj->initialize();

        // run background noise estimator
        // if (device_type == "leaf")
        //     usrp_obj->collect_background_noise_powers();

        OTAC_class otac_obj(*usrp_obj, *parser, device_id, device_type, input_data, dmin, dmax, num_leafs, stop_signal_called);

        if (!otac_obj.initialize())
        {
            LOG_WARN("OTAC class object initilization failed!");
            program_ends.store(true);
            return;
        }

        if (msg == "start")
        {
            LOG_INFO("----------- Starting OTAC program ---------------");
            otac_obj.run_proto();
        }
        else if (msg == "stop")
        {
            LOG_INFO("----------- Stopping OTAC program ---------------");
            otac_obj.stop();
        }

        while (!otac_obj.otac_routine_ends)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

        program_ends.store(true);
    };
    auto control_otac_topic = mqttClient.topics->getValue_str("otac") + device_id;
    mqttClient.setCallback(control_otac_topic, control_otac_callback, true);

    // run contoller on a separate thread
    std::string counterpart_id;
    if (device_type == "leaf")
        counterpart_id = parser->getValue_str("cent-id");
    bool is_cent = device_type == "cent";

    while (not stop_signal_called)
    {
        if (device_type == "cent" && program_ends)
        {
            // std::thread input_thread([&device_id, &counterpart_id, &is_cent]()
            //                          { gen_mqtt_control_msg(device_id, counterpart_id, is_cent); });
            // input_thread.join();
            gen_mqtt_control_msg(device_id, counterpart_id, is_cent);
            program_ends.store(false);
        }
        else if (device_type == "leaf" && program_ends)
        {
            LOG_INFO("Waiting for command from central node ...");
            program_ends.store(false);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return EXIT_SUCCESS;
};