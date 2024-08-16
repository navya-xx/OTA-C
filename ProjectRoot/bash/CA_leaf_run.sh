#!/bin/bash

mkdir -p $HOME/OTA-C/ProjectRoot/storage/logs/
cd $HOME/OTA-C/ProjectRoot/build/ 
node_serial=$1
node_id=$2

while true; do

    ./CA_leaf ${node_serial} ${node_id} | tee $HOME/OTA-C/ProjectRoot/storage/logs/leaf_output.log

    exit_status=$?

    sleep 1
    pkill -9 CA_leaf

    if [ $exit_status -eq 130 ]; then
        echo "Program stopped by Ctrl+C. Exiting loop."
        break
    fi

    echo "Program exited with status $exit_status. Restarting in 3 seconds..."
    sleep 3

done