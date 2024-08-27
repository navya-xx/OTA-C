#include "MQTTClient.hpp"

// Initialize the static instance pointer to nullptr
MQTTClient *MQTTClient::instance = nullptr;

// Define the fixed server address
const std::string MQTTClient::serverAddress = "tcp://192.168.5.247:1883";

static const int qos_level = 1;

// Public method to get the single instance of the class
MQTTClient &MQTTClient::getInstance(const std::string &clientId)
{
    if (!instance)
    {
        instance = new MQTTClient(clientId);
    }
    return *instance;
}

// Private constructor with fixed server address
MQTTClient::MQTTClient(const std::string &clientId)
    : client(serverAddress, clientId)
{
    connectOptions.set_clean_session(true);
    connectOptions.set_keep_alive_interval(20);
    client.set_message_callback([this](mqtt::const_message_ptr msg)
                                { onMessage(msg->get_topic(), msg->to_string()); });
    connect();
}

// Connect to the MQTT broker
bool MQTTClient::connect()
{
    try
    {
        client.connect(connectOptions)->wait();
        LOG_INFO_FMT("Connected to MQTT broker at  %1%", serverAddress);
        return true;
    }
    catch (const mqtt::exception &e)
    {
        LOG_WARN_FMT("Error connecting to MQTT broker:  %1%", e.what());
        return false;
    }
}

// Publish a message to a topic
bool MQTTClient::publish(const std::string &topic, const std::string &message, bool retained)
{
    try
    {
        client.publish(topic, message, qos_level, retained)->wait();
        LOG_INFO_FMT("Message published to topic:  %1%", topic);
        return true;
    }
    catch (const mqtt::exception &e)
    {
        LOG_WARN_FMT("Error publishing message:  %1%", e.what());
        return false;
    }
}

// Subscribe to a topic
bool MQTTClient::subscribe(const std::string &topic)
{
    try
    {
        client.subscribe(topic, qos_level)->wait();
        LOG_INFO_FMT("Subscribed to topic:  %1%", topic);
        return true;
    }
    catch (const mqtt::exception &e)
    {
        LOG_WARN_FMT("Error subscribing to topic:  %1%", e.what());
        return false;
    }
}

void MQTTClient::unsubscribe(const std::string &topic)
{
    try
    {
        client.unsubscribe(topic)->wait();
        callbacks.erase(topic);
        LOG_INFO_FMT("Unsubscribed from topic: %1%", topic);
    }
    catch (const mqtt::exception &e)
    {
        LOG_WARN_FMT("Error unsubscribing from topic: %1%", e.what());
    }
}

// Set a callback for incoming messages
void MQTTClient::setCallback(const std::string &topic, const std::function<void(const std::string &)> &callback)
{
    callbacks[topic] = callback;
    if (!subscribe(topic))
        callbacks.erase(topic);
}

// Internal callback function that processes incoming messages
void MQTTClient::onMessage(const std::string &topic, const std::string &payload)
{
    auto it = callbacks.find(topic);
    if (it != callbacks.end())
    {
        it->second(payload); // Call the stored callback function
    }
    else
    {
        LOG_WARN_FMT("No callback set for topic:  %1%", topic);
    }
}

// Function to get the current time as a string
std::string MQTTClient::getCurrentTimeString() const
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm = *std::localtime(&now_time);

    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}