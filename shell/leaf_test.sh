#!/bin/bash
NUM_MC_RUNS=10
run_sleep_duration=10
sync_window_minutes=3

#--------- Get Serial Number of Devices -------------------------------------------------------------------------------
leaf_node_id_file="id_leafs.conf"
serial_number=""
id=""

serial_number_exists() {
    local serial_to_check="$1"
    local uhd_output=$(uhd_find_devices --args "type=b200")
    
    if echo "$uhd_output" | grep -q "$serial_to_check"; then
        return 0  # Serial number exists in uhd_find_devices output
    else
        return 1  # Serial number does not exist in uhd_find_devices output
    fi
}

result=$(awk -v username="$USER" '$1 == username { print $2, $3 }' "$leaf_node_id_file")

if [ -z "$result" ]; then
    echo "No entry found in leaf_nodes_id.conf for username: $USER"
else
    # Read serial number and ID from the result and store in variables
    read serial_number id <<< "$result"
    echo "Serial number for username '$USER': $serial_number"
    echo "ID for username '$USER': $id"

    if serial_number_exists "$serial_number"; then
        echo "Device with serial number '$serial_number' found."
    else
        echo "ERROR: Device with serial number '$serial_number' not found."
        quit
    fi
fi

#----------------------------------------------------------------------------------------------------------------------

#---------- Synchronize (roughly) the start of script execution remotely ----------------------------------------------
# Define the synchronization window duration (in minutes)

# Function to get the current minute component of the time
get_current_minute() {
    date +%M
}

# Calculate the synchronization point (nearest multiple of 3 within the next sync_window_minutes minutes)
calculate_sync_point() {
    local current_minute=$(get_current_minute)
    local remainder="$(( current_minute % sync_window_minutes ))"
    local target_minute

    if [[ $remainder -eq 0 ]]; then
        target_minute="$(( current_minute + sync_window_minutes ))"  # Move to the next multiple of 3
    else
        target_minute="$(( current_minute + (sync_window_minutes - remainder) ))"  # Move to the nearest multiple of 3
    fi

    echo "$target_minute"
}

# Get the synchronization point (nearest multiple of 3 within the next sync_window_minutes minutes)
sync_point=$(calculate_sync_point)

# Calculate the delay (in seconds) until the synchronization point
current_time=$(date +%s)
target_time=$(date -d "$(date +'%Y-%m-%d %H'):$sync_point" +%s)
delay="$(( target_time - current_time ))"

# Wait until the synchronization point
echo "Waiting for ($delay) seconds until $(date -d @$target_time +'%H:%M:%S')..."
for (( i = $delay; i > 0; i-- ))
do
    echo -ne "Remaining time : $i seconds\033[0K\r"
    sleep 1
done

# Execute your program here
echo "Wait over! Starting program at time $(date +'%H:%M:%S')."
#----------------------------------------------------------------------------------------------------------------------

#--------------- Program execution ------------------------------------------------------------------------------------

LOGFOLDER="$HOME/OTA-C/shell/LogFolder"
if [ ! -d "$LOGFOLDER" ]; then
    mkdir -p "$LOGFOLDER"
fi

screen_name="leaf_node_${serial_number}"
program_name="test_cyclestartdetector"

for (( i=0; i < $NUM_MC_RUNS; i++ ))
do
    echo -e "\n RUN $i at $(date +'%H:%M:%S') \n"
    cmd_main="screen -L -Logfile ${LOGFOLDER}/logfile_${serial_number}_${i}.log -S ${screen_name} -d -m ${HOME}/OTA-C/cpp/build/${program_name} serial=${serial_number} ${id}"
    echo -e "RUN -> \t${cmd_main}\n"

    eval "${cmd_main}"

    sleep "$run_sleep_duration"

    # kill unfinished process
    if screen -list | grep -q "$screen_name"
    then
        session_id=$(screen -list | grep "$screen_name" | awk '{print $1}')
        echo "Screen '$screen_name' session id '$session_id' is still ACTIVE."
        if screen -S "$session_id" -X stuff "pgrep ${program_name}$(printf '\r')" > /dev/null 2>&1;
        then
            screen -X -S "$session_id" stuff "^C"
            echo "Closing program gracefully via '^C'"
            sleep 1
        else
            echo "${program_name} is not running."
            sleep 2
            continue
        fi
        if screen -S "$session_id" -X stuff "pgrep ${program_name}$(printf '\r')" > /dev/null 2>&1
        then
            echo "Closing program gracefully -> Failed!"
            screen -X -S "$session_id" stuff "^X"
            echo "Closing program forcefully via '^X'"
            sleep 1
            # Kill the program with SIGKILL (kill -9)
            screen -S "$session_id" -X stuff "pkill -9 ${program_name}$(printf '\r')"
            echo "Kill program forcefully via 'pkill -9 ${program_name}'"
            sleep 1
        else
            echo "Closing program gracefully -> Success!"
            sleep 1
        fi
    else
        echo "Screen '$screen_name' session id '$session_id' is INACTIVE."
        sleep 2
    fi
done