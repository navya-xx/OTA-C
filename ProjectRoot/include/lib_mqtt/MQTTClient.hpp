#ifndef MQTTCLIENT
#define MQTTCLIENT

#include "pch.hpp"
#include "log_macros.hpp"
#include <sstream>

class MQTTClient : public mqtt::callback
{
public:
    // Singleton access method
    static MQTTClient &getInstance(const std::string &clientId = "nuc");

    // Callback management
    void setMessageCallback(const std::string &topic, std::function<void(const std::string &)> callback);
    bool isConnected() const;

    // Telemetry info
    void publish(const std::string &topic, const std::string &payload);
    void subscribe(const std::string &topic);

    MQTTClient(const std::string &serverAddress, const std::string &clientId);
    ~MQTTClient();

private:
    // MQTT Client
    mqtt::async_client client;
    mqtt::connect_options connectOptions;
    std::string clientId;

    // Static map to hold instances by client ID
    static std::map<std::string, std::unique_ptr<MQTTClient>> instances;
    static std::string currentClientId; // Store the current client ID

    void connect();
    void disconnect();

    std::string getCurrentTimeString() const; // Function to get current time as a string

    // Callback map
    std::map<std::string, std::function<void(const std::string &)>> messageCallbacks;

    // Connection handling
    void onConnect(const mqtt::token &tok);
    void onDisconnect(const mqtt::token &tok);
    void onMessageArrived(mqtt::const_message_ptr msg);

    // Prevent copying and assignment
    MQTTClient(const MQTTClient &) = delete;
    MQTTClient &operator=(const MQTTClient &) = delete;
};

#endif // MQTTCLIENT