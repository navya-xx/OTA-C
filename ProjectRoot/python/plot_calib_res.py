import sqlite3
import json
import matplotlib.pyplot as plt
import sys
import numpy as np
from datetime import datetime

# Database and table details
DB_FILE = '/home/nuc/OTA-C/ProjectRoot/config/mosquitto/telemetry.db'
TABLE_NAME = 'mqtt_messages'

def get_data_from_db(serial):
    """Fetches data from the SQLite database for the given serial where is_processed = 0."""
    
    # Prepare the topic format with the provided serial
    topic_pattern = f"telemetry/powcalib/{serial}"

    # Connect to the SQLite database
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()

    # SQL query to select rows based on topic and is_processed = 0
    query = f"SELECT id, payload FROM {TABLE_NAME} WHERE topic = ? AND is_processed = 0"
    cursor.execute(query, (topic_pattern,))

    rows = cursor.fetchall()
    conn.close()

    return rows

def parse_json_payloads(rows):
    """Parses the JSON payloads and extracts rx_pow and tx_scale."""
    rx_pow_list = []
    tx_scale_list = []
    ids = []

    for row in rows:
        id = row[0]  # id of the row
        payload_json = row[1]  # payload is the second column
        try:
            data = json.loads(payload_json)
            # Extract rx_pow, tx_scale from the JSON data
            rx_pow = data.get("rx_pow")
            tx_scale = data.get("tx_scale")
            if rx_pow is not None and tx_scale is not None:
                rx_pow_list.append(rx_pow)
                tx_scale_list.append(tx_scale)
                ids.append(id)
        except json.JSONDecodeError as e:
            print(f"Error decoding JSON: {e}")
            continue

    return tx_scale_list, rx_pow_list, ids

def mark_rows_as_processed(ids):
    """Marks the rows as processed by updating the is_processed column."""
    if not ids:
        return  # Nothing to process

    # Connect to the SQLite database
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()

    # SQL query to update is_processed to 1 for the rows with the given ids
    query = f"UPDATE {TABLE_NAME} SET is_processed = 1 WHERE id = ?"
    
    for id in ids:
        cursor.execute(query, (id,))

    # Commit the changes and close the connection
    conn.commit()
    conn.close()

def plot_data(tx_scale, rx_pow, x_fit, y_fit, serial):
    """Plots tx_scale vs. rx_pow."""
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"{serial}_calib_pow_plot_{timestamp}.png"
    plt.figure(figsize=(8, 6))
    plt.scatter(tx_scale, np.sqrt(rx_pow), color='blue', label="data points")
    plt.plot(x_fit, y_fit, label=f'Polynomial Fit (degree=4)', color='green')
    tx_scale_max_id = np.argmax(tx_scale);
    plt.plot([0.0, tx_scale[tx_scale_max_id]], [0.0, np.sqrt(rx_pow)[tx_scale_max_id]], '-r', label="x=y")
    plt.xlabel('TX scale')
    plt.ylabel('RX scale')
    plt.title('Post-calibration signal scaling - %s' %serial)
    plt.legend();
    plt.grid(True)
    plt.show()

    plt.savefig("plots/" + filename)

def polynomial_fit(x, y, degree):
    """
    Perform polynomial curve fitting.
    
    Args:
        x: array-like, independent variable data (x values)
        y: array-like, dependent variable data (y values)
        degree: int, degree of the polynomial to fit

    Returns:
        p: numpy poly1d object representing the polynomial
        x_fit: array-like, fitted x values
        y_fit: array-like, fitted y values
    """
    # Fit polynomial of the given degree
    p_coeff = np.polyfit(x, y, degree)
    p = np.poly1d(p_coeff)

    # Generate fitted values
    x_fit = np.linspace(min(x), max(x), 100)
    y_fit = p(x_fit)

    return p, x_fit, y_fit



def main(serial):
    # Step 1: Get the data from the database
    rows = get_data_from_db(serial)

    # Step 2: Parse the JSON payloads
    tx_scale_list, rx_pow_list, ids = parse_json_payloads(rows)

    # Step 3: curve-fit and Plot the data if available
    if tx_scale_list and rx_pow_list:
        p, x_fit, y_fit = polynomial_fit(tx_scale_list, np.sqrt(rx_pow_list), 5);
        plot_data(tx_scale_list, rx_pow_list, x_fit, y_fit, serial)
        
        # Step 4: Mark the rows as processed
        mark_rows_as_processed(ids)
    else:
        print("No data found or parsed.")

if __name__ == '__main__':
    # Read serial from input
    if len(sys.argv) != 2:
        print("Usage: python script.py <serial>")
        sys.exit(1)

    serial = sys.argv[1]
    main(serial)