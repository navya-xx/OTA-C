#!/usr/bin/python3

import sqlite3
import paho.mqtt.client as mqtt
import json
import pandas as pd
from datetime import datetime, timedelta

# MQTT settings
MQTT_BROKER = 'localhost'
MQTT_PORT = 1883
CALIB_TOPIC = 'calibration/results'

# SQLite settings
DB_FILE = '/home/nuc/OTA-C/ProjectRoot/config/mosquitto/telemetry.db'

# Devices config and list
DEV_CONF = '/home/nuc/OTA-C/ProjectRoot/config/devices.json'

def gen_device_list():
    with open(DEV_CONF, 'r') as file:
        dev_data = json.load(file)
    dev_list = {}
    dev_list["cent"] = dev_data["cent-node"]["serial"]
    dev_list["leafs"] = []
    for d in dev_data["leaf-nodes"]:
        dev_list["leafs"].append(d["serial"])
    return dev_list

def process_calibration_data(dataframe):
    dev_list = gen_device_list()
    cent = dev_list["cent"]
    calib_results = pd.DataFrame(columns=['total_runs', 'cent', 'leaf', 'cent_tx_gain', 'leaf_rx_gain', 'amp_ratio_mean', 'amp_ratio_var', 'time'])
    for leaf in dev_list["leafs"]:
        # c -> l
        c_to_l_df = dataframe[(dataframe['tx_dev'] == cent) & (dataframe['rx_dev'] == leaf)].rename(columns={'amplitude':'amp_c_to_l'})
        l_to_c_df = dataframe[(dataframe['tx_dev'] == leaf) & (dataframe['rx_dev'] == cent)].rename(columns={'amplitude':'amp_l_to_c'})
        result = pd.merge_asof(
            c_to_l_df.sort_values('time'),
            l_to_c_df.sort_values('time'),
            on='time',
            direction='nearest',
            tolerance=pd.Timedelta(seconds=3)
        ).dropna()
        
        store_df = pd.DataFrame(columns=['run_count', 'cent', 'leaf', 'cent_tx_gain', 'leaf_rx_gain', 'amp_c_to_l', 'amp_l_to_c', 'amp_ratio', 'time'])
        for c in range(result.shape[0]):
            tmp_dict = {}
            tmp_dict['run_count'] = c
            tmp_dict['cent'] = cent
            tmp_dict['leaf'] = leaf
            tmp_dict['cent_tx_gain'] = result['tx_gain_x'].values[c]
            tmp_dict['leaf_rx_gain'] = result['rx_gain_x'].values[c]
            # find the entry from l_to_c closest in time to c_to_l
            tmp_dict['amp_c_to_l'] = result['amp_c_to_l'].values[c]
            tmp_dict['amp_l_to_c'] = result['amp_l_to_c'].values[c]
            tmp_dict['amp_ratio'] = result['amp_c_to_l'].values[c] / result['amp_l_to_c'].values[c]
            tmp_dict['time'] = result['time'].values[c]
            store_df = pd.concat([store_df, pd.DataFrame([tmp_dict])], ignore_index=True)
        
        # compute mean and variance of the data
        amp_c_to_l_mean = store_df['amp_c_to_l'].mean()
        amp_c_to_l_var = store_df['amp_c_to_l'].var()
        amp_l_to_c_mean = store_df['amp_l_to_c'].mean()
        amp_l_to_c_var = store_df['amp_l_to_c'].var()
        ratio_amp_mean = amp_c_to_l_mean / amp_l_to_c_mean
        amp_mean = store_df['amp_ratio'].mean()
        amp_var = store_df['amp_ratio'].var() / (amp_mean ** 2)
        total_runs = store_df.shape[0]
        if total_runs == 0:
            print("%s: No calibration data available!" %leaf)
            continue
        
        new_df = pd.DataFrame([{
            'total_runs':total_runs, 
            'cent':cent, 'leaf':leaf, 
            'cent_tx_gain':store_df['cent_tx_gain'].values[0], 
            'leaf_rx_gain':store_df['leaf_rx_gain'].values[0], 
            'amp_c_to_l_mean': amp_c_to_l_mean,
            'amp_c_to_l_var': amp_c_to_l_var,
            'amp_l_to_c_mean': amp_l_to_c_mean, 
            'amp_l_to_c_var': amp_l_to_c_var,
            'ratio_amp_mean': ratio_amp_mean, 
            'amp_ratio_mean':amp_mean,
            'amp_ratio_var':amp_var, 
            'time':store_df['time'].min().strftime('%Y-%m-%d %H:%M:%S')
            }])
        calib_results = pd.concat([calib_results, new_df], ignore_index=True)

    return calib_results

        

def publish_processed_data(client, topic, data):
    # Convert the processed data to JSON format
    payload = json.dumps(data)
    
    # Publish the processed data to the MQTT topic with retained flag
    client.publish(topic, payload, retain=True)
    print(f"Published processed data to {topic}: {payload}")

def main():
    # Connect to SQLite database
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    calibration_data = pd.DataFrame()

    mqtt_publish_list = []
    database_update_queries = []

    # last_timestamp = datetime.strptime("2024-08-26 14:00:00", '%Y-%m-%d %H:%M:%S')
    last_timestamp = datetime.now() - timedelta(minutes=30)

    try:
        # Query to select all messages from the table
        cursor.execute("SELECT id, topic, payload, timestamp, is_processed FROM mqtt_messages")

        for row in cursor.fetchall():
            id, topic, payload, timestamp, is_processed = row
            row_time = datetime.strptime(timestamp, '%Y-%m-%d %H:%M:%S')
            
            if (topic == CALIB_TOPIC and (is_processed == 0 and row_time > last_timestamp)):
                payload = eval(payload)
                new_data_df = pd.DataFrame([payload])
                new_data_df['time'] = pd.to_datetime(new_data_df['time'], format='%Y-%m-%d %H:%M:%S')
                calibration_data = pd.concat([calibration_data, new_data_df], ignore_index=True)
                if (is_processed == 0):
                    database_update_queries.append("UPDATE mqtt_messages SET is_processed = 1 WHERE id = %d"%id)
            
        if calibration_data.shape[0] == 0:
            raise Exception("No calibration data available at the moment.")
            
        
        # conn.commit()

        # Process calibration data
        calib_res = process_calibration_data(calibration_data)

        # insert data into calibration table
        cursor.execute('''
          CREATE TABLE IF NOT EXISTS calib_mean_results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            cent TEXT NOT NULL,
            leaf TEXT NOT NULL,
            cent_tx_gain NUMERIC,
            leaf_rx_gain NUMERIC,
            amp_c_to_l_mean REAL,
            amp_c_to_l_var REAL,
            amp_l_to_c_mean REAL,
            amp_l_to_c_var REAL,
            ratio_amp_mean REAL,
            amp_ratio_mean REAL NOT NULL,
            amp_ratio_var REAL NOT NULL,
            total_runs INTEGER,
            timestamp TEXT NOT NULL
          );
          ''')
        
        insert_query = '''
                        INSERT INTO calib_mean_results (cent, leaf, cent_tx_gain, leaf_rx_gain, amp_c_to_l_mean, amp_c_to_l_var, amp_l_to_c_mean, amp_l_to_c_var, amp_ratio_mean, amp_ratio_var, total_runs, timestamp)
                        VALUES (:cent, :leaf, :cent_tx_gain, :leaf_rx_gain, :amp_c_to_l_mean, :amp_c_to_l_var, :amp_l_to_c_mean, :amp_l_to_c_var, :ratio_amp_mean, :amp_ratio_mean, :amp_ratio_var, :total_runs, :time)
                        '''
        for i, cdata in calib_res.iterrows():
            cdict = cdata.to_dict()
            cursor.execute(insert_query, cdict)
            # Also, publish to a topic that the leaf nodes can subscribe to
            topic = "calibration/ratio/" + cdict['cent'] + "/" + cdict['leaf']
            mqtt_publish_list.append({'topic':topic, 'payload':json.dumps(cdict)})

        conn.commit()

        # update database
        if len(database_update_queries) > 0:
            for q in database_update_queries:
                cursor.execute(q)
            
            conn.commit()
        
    except sqlite3.Error as e:
        print(f"SQLite error: {e}")
    
    finally:
        # Close the SQLite connection
        conn.close()
    
    
    # Connect to MQTT broker
    if len(mqtt_publish_list) > 0:
        mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2, client_id="calib_result_publisher")
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        for mdata in mqtt_publish_list:
            result = mqtt_client.publish(mdata['topic'], mdata['payload'], retain=True)
            if result.rc == 0:
                print(f"Message '{mdata['payload']}' sent successfully to topic '{mdata['topic']}'")
            else:
                print(f"Failed to send message to topic '{mdata['topic']}', return code: {result.rc}")
        # Stop MQTT loop and disconnect
        mqtt_client.disconnect()


if __name__ == '__main__':
    main()