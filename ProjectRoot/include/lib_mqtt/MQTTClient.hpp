#ifndef MQTTCLIENT
#define MQTTCLIENT

#include "pch.hpp"
#include "log_macros.hpp"

class MQTTClient : public mqtt::callback
{
public:
    // Singleton access method
    static MQTTClient &getInstance();

    // Callback management
    void setMessageCallback(const std::string &topic, std::function<void(const std::string &)> callback);
    bool isConnected() const;

    // Telemetry info
    void publish(const std::string &topic, const std::string &payload);
    void subscribe(const std::string &topic);

private:
    MQTTClient(const std::string &serverAddress, const std::string &clientId);
    ~MQTTClient();

    // MQTT Client
    mqtt::async_client client;
    mqtt::connect_options connectOptions;

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