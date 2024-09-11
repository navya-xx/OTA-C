#!/bin/bash

CLEAN_SLATE=false

if [ $# -gt 0 ] && [ "$1" -eq 1 ]; then
    CLEAN_SLATE=true
fi

if $CLEAN_SLATE; then
    rm -rf $HOME/OTA-C/ProjectRoot/storage/calibration/*
    sleep 1
fi


cent_node="3237C3E"
# leaf_node_serials=("32C79F7" "32B172B" "32C793E" "32B1708" "337D42D" "32C7920" "32C79BE" "32C79C6" "32C7981")
leaf_node_serials=("32C79BE")

# remote_nodes=( "rpi4m1@192.168.5.241" "rpi4m1@192.168.5.241" "rpi4m2@192.168.5.242" "rpi4m3@192.168.5.243" "rpi4m4@192.168.5.244" "rpi4m5@192.168.5.245" "rpi4compute@192.168.5.246" "nuc01@192.168.5.248" "nuc01@192.168.5.248" )
remote_nodes=("nuc01@192.168.5.248")

cleanup() {
    echo "Caugth SIGINT ! Performing cleanup..."
    for (( j=0; j<${#remote_nodes[@]}; j++ ))
    do
        node_name="${remote_nodes[$i]}"
        if [ "${node_name}" == "" ]; then
            bash -c 'pkill -9 CA_'
        else
            ssh ${node_name} "bash -c 'pkill -9 CA_'"
        fi
        sleep 1
    done
    exit 0
}

trap cleanup SIGINT

timeout=900  # Set your timeout in seconds

for (( i=0; i<${#remote_nodes[@]}; i++ ))
do

    node_name="${remote_nodes[$i]}"
    node_serial="${leaf_node_serials[$i]}"

    echo "Iteration $i for leaf node ${node_serial}: Starting..."

    # Start two commands in detached screen sessions
    if [ "${node_name}" == "" ]; then
        bash -c 'pkill -9 CA_'
    else
        ssh ${node_name} "bash -c 'pkill -9 CA_'"
    fi
    sleep 1
    
    if $CLEAN_SLATE; then
        if [ "${node_name}" != "" ]; then
            ssh ${node_name} "bash -c 'rm -rf \$HOME/OTA-C/ProjectRoot/storage/calibration/*'"
        fi
        sleep 1
    fi

    if [ "${node_name}" == "" ]; then
        screen -dmL -Logfile "$HOME/OTA-C/ProjectRoot/storage/calibration/session_leaf_${node_serial}.log" -S session_leaf_${node_serial} bash -c "cd $HOME/OTA-C/ProjectRoot/build && ./CA_calib leaf ${node_serial} ${cent_node}"
        sleep 1
        screen -dmL -Logfile "$HOME/OTA-C/ProjectRoot/storage/calibration/session_cent_${node_serial}.log" -S session_cent_${node_serial} bash -c "cd $HOME/OTA-C/ProjectRoot/build/ && ./CA_calib cent ${cent_node} ${node_serial}"
    else
        screen -dmL -Logfile "$HOME/OTA-C/ProjectRoot/storage/calibration/session_leaf_${node_serial}.log" -S session_leaf_${node_serial} bash -c "ssh ${node_name} 'cd \$HOME/OTA-C/ProjectRoot/build && ./CA_calib leaf ${node_serial} ${cent_node}'"
        sleep 1
        screen -dmL -Logfile "$HOME/OTA-C/ProjectRoot/storage/calibration/session_cent_${node_serial}.log" -S session_cent_${node_serial} bash -c "cd $HOME/OTA-C/ProjectRoot/build/ && ./CA_calib cent ${cent_node} ${node_serial}"
    fi

    start_time=$(date +%s)
    while true; do
        current_time=$(date +%s)
        elapsed_time=$((current_time - start_time))

        # Check if either session has finished
        if ! screen -list | grep -q "session_leaf_${node_serial}"; then
            echo "session_leaf_${node_serial} finished."
            break
        elif ! screen -list | grep -q "session_cent_${node_serial}"; then
            echo "session_leaf_${node_serial} finished."
            break
        elif [ $elapsed_time -ge $timeout ]; then
            echo "Timeout reached in Iteration $i."
            break
        fi

        sleep 1  # Check every second
    done

    # Optionally, kill the other session if still running after timeout
    screen -S session_leaf_${node_serial} -X quit 2>/dev/null
    screen -S session_cent_${node_serial} -X quit 2>/dev/null

    echo "Iteration $i: Finished."
done

echo "Calibration Finished!"