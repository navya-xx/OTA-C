#include "MQTTClient.hpp"

// Define fixed server address and client ID
static const std::string SERVER_ADDRESS = "tcp://192.168.5.247:1883";
static const std::string CLIENT_ID = "test_nuc";

// Singleton access method
MQTTClient &MQTTClient::getInstance()
{
    static MQTTClient instance(SERVER_ADDRESS, CLIENT_ID);
    return instance;
}

// Private Constructor
MQTTClient::MQTTClient(const std::string &serverAddress, const std::string &clientId)
    : client(serverAddress, clientId)
{
    connectOptions.set_clean_session(true);
    connectOptions.set_keep_alive_interval(20);
    client.set_callback(*this);
    connect();
}

// Private Destructor
MQTTClient::~MQTTClient()
{
    if (client.is_connected())
    {
        disconnect();
    }
}

// Connect to the MQTT broker
void MQTTClient::connect()
{
    try
    {
        client.connect(connectOptions)->wait();
        LOG_DEBUG("Connected to MQTT broker.");
    }
    catch (const mqtt::exception &exc)
    {
        LOG_WARN_FMT("Connection error: %1%", exc.what());
    }
}

// Disconnect from the MQTT broker
void MQTTClient::disconnect()
{
    try
    {
        client.disconnect()->wait();
        LOG_DEBUG_FMT("Disconnected from MQTT broker.");
    }
    catch (const mqtt::exception &exc)
    {
        LOG_WARN_FMT("Disconnection error: ", exc.what());
    }
}

// Check if connected
bool MQTTClient::isConnected() const
{
    return client.is_connected();
}

// Set message callback for a specific topic
void MQTTClient::setMessageCallback(const std::string &topic, std::function<void(const std::string &)> callback)
{
    messageCallbacks[topic] = callback;
    subscribe(topic);
}

// Publish a message to a topic with a timestamp
void MQTTClient::publish(const std::string &topic, const std::string &payload)
{
    connect(); // Ensure connection before publishing

    std::string timestampedPayload;
    std::string timestamp = getCurrentTimeString(); // Get the current time

    try
    {
        // Parse the input payload into JSON
        json jsonPayload = json::parse(payload);

        // Add the timestamp to the JSON object
        jsonPayload["time"] = timestamp;

        // Convert the JSON object back to a string
        timestampedPayload = jsonPayload.dump();
    }
    catch (const json::parse_error &e)
    {
        // Handle JSON parsing error
        LOG_WARN_FMT("JSON parse error: %1%", e.what());

        // Optionally, you could publish the original payload with a basic error structure
        timestampedPayload = R"({"error": "Invalid JSON", "original_payload": ")" + payload + R"(", "time": ")" + timestamp + R"("})";
    }

    try
    {
        client.publish(topic, timestampedPayload, 1, false);
        LOG_DEBUG_FMT("Published message to %1% : %2%", topic, timestampedPayload);
    }
    catch (const mqtt::exception &exc)
    {
        LOG_WARN_FMT("Publish error: %1%", exc.what());
    }
}

// Subscribe to a topic
void MQTTClient::subscribe(const std::string &topic)
{
    connect();
    try
    {
        client.subscribe(topic, 1);
        LOG_DEBUG_FMT("Subscribed to topic %1%", topic);
    }
    catch (const mqtt::exception &exc)
    {
        LOG_WARN_FMT("Subscribe error: %1%", exc.what());
    }
}

// Handle messages arriving on subscribed topics
void MQTTClient::onMessageArrived(mqtt::const_message_ptr msg)
{
    std::string topic = msg->get_topic();
    std::string payload = msg->get_payload_str();

    auto it = messageCallbacks.find(topic);
    if (it != messageCallbacks.end())
    {
        it->second(payload);
    }
    else
    {
        LOG_WARN_FMT("Received message on unknown topic %1% : %2%", topic, payload);
    }
}

// Connection callback
void MQTTClient::onConnect(const mqtt::token &tok)
{
    LOG_DEBUG("Connected with MQTT broker.");
}

// Disconnection callback
void MQTTClient::onDisconnect(const mqtt::token &tok)
{
    LOG_DEBUG("Disconnected from MQTT broker.");
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