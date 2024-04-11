#!/usr/bin/python3

"""OTA-C scheme implementation"""

# 1. Synchronize clocks via REF signal from one of the devices
# 2. Prepare Tx and Rx threads
# 3. Flip a coin to chose whether to transmit or receive
# 4.a If transmitting, prepare transmit signal and transmit on a specific resource
# 4.b If receiving, update currect estimate with received information
# 5. Repeat from step 3


