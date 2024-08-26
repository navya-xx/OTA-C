#ifndef MQTTCLIENT
#define MQTTCLIENT

#include "pch.hpp"
#include "log_macros.hpp"

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

    // Set a callback for incoming messages
    void setCallback(const std::string &topic, const std::function<void(const std::string &)> &callback);

    std::string getCurrentTimeString() const;

private:
    // Private constructor with fixed server address
    MQTTClient(const std::string &clientId);

    // Prevent copying
    MQTTClient(const MQTTClient &) = delete;
    MQTTClient &operator=(const MQTTClient &) = delete;

    // Internal callback for MQTT messages
    void onMessage(const std::string &topic, const std::string &payload);

    // MQTT client instance
    mqtt::async_client client;
    mqtt::connect_options connectOptions;

    // Map to store topic and associated callback
    std::map<std::string, std::function<void(const std::string &)>> callbacks;

    // Static instance pointer
    static MQTTClient *instance;

    // Fixed server address
    static const std::string serverAddress;
};

#endif // MQTTCLIENT