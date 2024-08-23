#!/usr/bin/python3

import paho.mqtt.client as mqtt
import sqlite3
import json
import time

list_of_topics = [
    "calibration/data/+",
    "config/run_config_info"
]

# SQLite setup
conn = sqlite3.connect('../config/mosquitto/telemetry.db')
c = conn.cursor()
c.execute('''CREATE TABLE IF NOT EXISTS messages
             (topic text, payload text, timestamp text)''')
conn.commit()

# Callback when a message is received
def on_message(client, userdata, msg):
    payload_str = msg.payload.decode('utf-8')
    timestamp = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())
    print(f"Received message on {msg.topic}: {payload_str}")

    # Insert message into the database
    c.execute("INSERT INTO messages (topic, payload, timestamp) VALUES (?, ?, ?)",
              (msg.topic, payload_str, timestamp))
    conn.commit()

# MQTT setup
client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2, client_id="TelemetryDb_client")
client.on_message = on_message

client.connect("localhost", 1883, 60)
for topic in list_of_topics:
    client.subscribe(topic)

# Start the loop
client.loop_forever()