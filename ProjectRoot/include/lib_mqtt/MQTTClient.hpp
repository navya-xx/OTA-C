#ifndef MQTTCLIENT
#define MQTTCLIENT

#include "pch.hpp"
#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"

class MQTTClient : public mqtt::callback
{
public:
    // Singleton access method
    static MQTTClient &getInstance(const std::string &clientId = "nuc");

    // Connect to the MQTT broker
    bool connect();

    // Publish a message to a topic
    bool publish(const std::string &topic, const std::string &message, bool retained = false);

    // Subscribe to a topic
    bool subscribe(const std::string &topic);
    void unsubscribe(const std::string &topic);

    // Set a callback for incoming messages
    void setCallback(const std::string &topic, const std::function<void(const std::string &)> &callback, bool run_in_thread = false);

    // Thread handler for MQTT message listening
    void startListening();
    void stopListening();
    void listenLoop();

    std::string getCurrentTimeString() const;
    bool pause_callbacks = false;

    std::string timestamp_float_data(const float &data);
    std::string timestamp_str_data(const std::string &data);

    std::unique_ptr<ConfigParser> topics;
    bool temporary_listen_for_last_value(std::string &val, const std::string &topic, const float &wait_count = 30, const size_t &wait_time = 50);

private:
    // Mutex for thread-safe implementation
    static std::mutex mqtt_mutex;
    static std::mutex callback_mutex;

    // Private constructor with fixed server address
    MQTTClient(const std::string &clientId);

    // Prevent copying
    MQTTClient(const MQTTClient &) = delete;
    MQTTClient &operator=(const MQTTClient &) = delete;

    // Internal callback for MQTT messages
    void onMessage(const std::string &topic, const std::string &payload);

    boost::thread mqttThread;
    bool isRunning;

    // MQTT client instance
    mqtt::async_client client;
    mqtt::connect_options connectOptions;

    // Map to store topic and associated callback
    std::unordered_map<std::string, std::pair<std::function<void(const std::string &)>, bool>> callbacks;

    // Static instance pointer
    static MQTTClient *instance;

    // Fixed server address
    static const std::string serverAddress;
};

#endif // MQTTCLIENT