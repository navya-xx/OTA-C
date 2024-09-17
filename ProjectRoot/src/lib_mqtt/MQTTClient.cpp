#include "MQTTClient.hpp"

std::mutex MQTTClient::mqtt_mutex;     // Define the static mutex
std::mutex MQTTClient::callback_mutex; // Define the static mutex

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
    : client(serverAddress, clientId), topics(nullptr)
{
    connectOptions.set_clean_session(true);
    connectOptions.set_keep_alive_interval(60);
    client.set_message_callback([this](mqtt::const_message_ptr msg)
                                { onMessage(msg->get_topic(), msg->to_string()); });
    connect();

    // topics parser
    auto homePath = get_home_dir();
    std::string topics_config_file = homePath + "/OTA-C/ProjectRoot/config/mqtt_topics.conf";
    topics = std::make_unique<ConfigParser>(topics_config_file);
}

// Connect to the MQTT broker
bool MQTTClient::connect()
{
    std::lock_guard<std::mutex> lock(mqtt_mutex); // Lock the mutex for thread safety
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
    std::lock_guard<std::mutex> lock(mqtt_mutex); // Lock the mutex for thread safety
    try
    {
        auto token = client.publish(topic, message, qos_level, retained);
        if (!token->wait_for(std::chrono::seconds(2)))
        {
            LOG_WARN("Publishing timed out!");
            return false;
        }
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
    std::lock_guard<std::mutex> lock(mqtt_mutex); // Lock the mutex for thread safety
    try
    {
        auto token = client.subscribe(topic, qos_level);
        if (!token->wait_for(std::chrono::seconds(2)))
        {
            LOG_WARN("Subscription timed out!");
            return false;
        }
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
void MQTTClient::setCallback(const std::string &topic, const std::function<void(const std::string &)> &callback, bool run_in_thread)
{
    std::lock_guard<std::mutex> lock(callback_mutex); // Lock the static mutex for thread safety
    callbacks[topic] = std::make_pair(callback, run_in_thread);
    if (!subscribe(topic))
        callbacks.erase(topic);
}

// Internal callback function that processes incoming messages
void MQTTClient::onMessage(const std::string &topic, const std::string &payload)
{
    std::lock_guard<std::mutex> lock(callback_mutex); // Lock the static mutex for thread safety
    auto it = callbacks.find(topic);
    if (it != callbacks.end())
    {
        if (!pause_callbacks)
        { // Check if this callback should run in a separate thread
            if (it->second.second)
            {
                // Run the callback in a separate thread
                std::thread([callback = it->second.first, payload]()
                            { callback(payload); })
                    .detach(); // Detach the thread to let it run independently
            }
            else
            {
                // Run the callback on the main thread (blocking)
                it->second.first(payload);
            }
        }
        else
            LOG_WARN("Callback are pause for the moment...");
    }
    else
    {
        LOG_WARN_FMT("No callback set for topic:  %1%", topic);
    }
}

// Start listening on a separate thread
void MQTTClient::startListening()
{
    isRunning = true;
    mqttThread = boost::thread(&MQTTClient::listenLoop, this);
}

// Stop listening thread
void MQTTClient::stopListening()
{
    isRunning = false;
    if (mqttThread.joinable())
    {
        mqttThread.join();
    }
}

// Loop to listen for messages
void MQTTClient::listenLoop()
{
    while (isRunning)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

std::string MQTTClient::timestamp_float_data(const float &data)
{
    std::string text = "{\"value\": \"" + floatToStringWithPrecision(data, 8) + "\", \"time\": \"" + getCurrentTimeString() + "\"}";
    return text;
}

std::string MQTTClient::timestamp_str_data(const std::string &data)
{
    std::string text = "{\"value\": \"" + data + "\", \"time\": \"" + getCurrentTimeString() + "\"}";
    return text;
}

/**
 * @brief Listens temporarily for the last value published on a specified MQTT topic.
 *
 * This function subscribes to the specified MQTT topic and listens for the last value for a specified duration.
 *
 * @param[out] val A reference to a string that will store the last received value from the MQTT topic.
 * @param[in] topic The MQTT topic to subscribe to and listen for the last published value.
 * @param[in] wait_count The maximum number of attempts to listen for the value before timing out.
 * @param[in] wait_time The time (in milliseconds) to wait between each listening attempt.
 *
 * @return true if a value is successfully received within the waiting time, false otherwise.
 */
bool MQTTClient::temporary_listen_for_last_value(std::string &val, const std::string &topic, const float &wait_count, const size_t &wait_time)
{
    // create a callback
    bool got_val;
    std::function<void(const std::string &)> callback = [&val, &got_val](const std::string &payload)
    {
        // Parse the JSON payload
        json jdata;
        try
        {
            jdata = json::parse(payload);
            val = jdata["value"];
            got_val = true;
        }
        catch (json::exception &e)
        {
            LOG_WARN_FMT("JSON parse error : %1%", e.what());
        }
    };
    setCallback(topic, callback);
    size_t timer = 0;
    while (got_val == false && timer < wait_count)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));
        timer++;
    }
    unsubscribe(topic);
    return got_val;
}