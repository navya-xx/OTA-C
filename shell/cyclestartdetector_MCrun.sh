#!/bin/bash

LEAF_DEVICES_NUC=( "32C793E" "32C79BE" "32C7981" "32B172B" )
LEAF_DEVICES_NUC01=( "32B1708" "32B1728" "32C7920" )
# LEAF_DEVICES_RPI4COMPUTE=( "32C79F7" )
CENT_DEVICE="32C79F7"
LEAF_DEVICES_RPI4MODULE=( "32C79C6" )

LEAF_DEVICES_NUC_ID=( 31 73 109 139 )
LEAF_DEVICES_NUC01_ID=( 97 173 211 )
# LEAF_DEVICES_RPI4COMPUTE_ID=( 139 )
LEAF_DEVICES_RPI4MODULE_ID=( 229 )

LEAF_SSH_PREFIX_NUC01="ssh nuc01@192.168.5.68"
LEAF_SSH_PREFIX_RPI4COMPUTE="ssh rpi4compute@192.168.5.96"
LEAF_SSH_PREFIX_RPI4MODULE="ssh uhdrpi4@192.168.5.198"
LEAF_SSH_PREFIX_CENT="${LEAF_SSH_PREFIX_RPI4COMPUTE}"
# LEAF_SSH_PREFIX_NUC01=""
# LEAF_SSH_PREFIX_RPI4COMPUTE=""

LEAF_HOME_NUC="/home/nuc"
LEAF_HOME_NUC01="/home/nuc01"
LEAF_HOME_RPI4COMPUTE="/home/rpi4compute"
LEAF_HOME_RPI4MODULE="/home/uhdrpi4"
LEAF_HOME_CENT="${LEAF_HOME_RPI4COMPUTE}"

LOGFOLDER="./LogFolder"

NUM_MC_RUNS=10

screen_name_list=()
program_list=()

for (( i=0; i < $NUM_MC_RUNS; i++ ))
do
    echo -e "\n RUN $i \n"

    if [[ -v LEAF_DEVICES_NUC ]]
    then
        echo -e "\n DEVICE NUC\n"
        dev_counter=0
        for nuc_dev in "${LEAF_DEVICES_NUC[@]}"
        do
            cmd_main="screen -L -Logfile ${LOGFOLDER}/logfile_${nuc_dev}_${i}.log -S csdtest_nuc_${nuc_dev} -d -m ${LEAF_HOME_NUC}/OTA-C/cpp/build/test_cyclestartdetector serial=${nuc_dev} ${LEAF_DEVICES_NUC_ID[$dev_counter]}"
            echo -e "\t Leaf Device ${LEAF_DEVICES_NUC_ID[$dev_counter]}"
            echo -e "RUN -> \t${cmd_main}\n"
            eval "${cmd_main}"
            if [[ $i -eq 0 ]]
            then
                screen_name_list+=("csdtest_nuc_${nuc_dev}")
                program_list+=("test_cyclestartdetector")
            fi
            (( dev_counter++ ))
        done
    fi

    if [[ -v LEAF_DEVICES_NUC01 ]]
    then
        echo -e "\n DEVICE NUC01\n"
        dev_counter=0
        for nuc01_dev in "${LEAF_DEVICES_NUC01[@]}"
        do
            cmd_main="screen -L -Logfile ${LOGFOLDER}/logfile_${nuc01_dev}_${i}.log -S csdtest_nuc01_${nuc01_dev} -d -m ${LEAF_SSH_PREFIX_NUC01} ${LEAF_HOME_NUC01}/OTA-C/cpp/build/test_cyclestartdetector serial=${nuc01_dev} ${LEAF_DEVICES_NUC01_ID[$dev_counter]}"
            echo -e "\t Leaf Device ${LEAF_DEVICES_NUC01_ID[$dev_counter]}"
            echo -e "RUN -> \t${cmd_main}\n"
            eval "${cmd_main}"
            if [[ $i -eq 0 ]]
            then
                screen_name_list+=( "csdtest_nuc01_${nuc01_dev}" )
                program_list+=( "test_cyclestartdetector" )
            fi
            (( dev_counter++ ))
        done
    fi

    if [[ -v LEAF_DEVICES_RPI4COMPUTE ]]
    then
        echo -e "\n DEVICE RPI4COMPUTE\n"
        dev_counter=0
        for rpi4compute_dev in "${LEAF_DEVICES_RPI4COMPUTE[@]}"
        do
            cmd_main="screen -L -Logfile ${LOGFOLDER}/logfile_${rpi4compute_dev}_${i}.log -S csdtest_rpi4compute_${rpi4compute_dev} -d -m ${LEAF_SSH_PREFIX_RPI4COMPUTE} ${LEAF_HOME_RPI4COMPUTE}/OTA-C/cpp/build/test_cyclestartdetector serial=${rpi4compute_dev} ${LEAF_DEVICES_RPI4COMPUTE_ID[$dev_counter]}"
            echo -e "\t Leaf Device ${LEAF_DEVICES_RPI4COMPUTE_ID[$dev_counter]}"
            echo -e "RUN -> \t${cmd_main}\n"
            eval "${cmd_main}"
            if [[ $i -eq 0 ]]
            then
                screen_name_list+=( "csdtest_rpi4compute_${rpi4compute_dev}" )
                program_list+=( "test_cyclestartdetector" )
            fi
            (( dev_counter++ ))
        done
    fi

    if [[ -v LEAF_DEVICES_RPI4MODULE ]]
    then
        echo -e "\n DEVICE RPI4MODULE\n"
        dev_counter=0
        for rpi4module_dev in "${LEAF_DEVICES_RPI4MODULE[@]}"
        do
            cmd_main="screen -L -Logfile ${LOGFOLDER}/logfile_${rpi4module_dev}_${i}.log -S csdtest_rpi4module_${rpi4module_dev} -d -m ${LEAF_SSH_PREFIX_RPI4MODULE} ${LEAF_HOME_RPI4MODULE}/OTA-C/cpp/build/test_cyclestartdetector serial=${rpi4module_dev} ${LEAF_DEVICES_RPI4MODULE_ID[$dev_counter]}"
            echo -e "\t Leaf Device ${LEAF_DEVICES_RPI4MODULE_ID[$dev_counter]}"
            echo -e "RUN -> \t${cmd_main}\n"
            eval "${cmd_main}"
            if [[ $i -eq 0 ]]
            then
                screen_name_list+=( "csdtest_rpi4module_${rpi4module_dev}" )
                program_list+=( "test_cyclestartdetector" )
            fi
            (( dev_counter++ ))
        done
    fi


    if [[ -v CENT_DEVICE ]]
    then
        sleep 2
        echo -e "\n DEVICE CENT\n"
        cmd_main="screen -L -Logfile ${LOGFOLDER}/logfile_${CENT_DEVICE}_${i}.log -S cent_${CENT_DEVICE} -d -m ${LEAF_SSH_PREFIX_CENT} ${LEAF_HOME_CENT}/OTA-C/cpp/build/tx_rx_zfc serial=${CENT_DEVICE} ${LEAF_HOME_CENT}/OTA-C/cpp/storage/new_mc_run_${i}_data.dat"
        echo -e "\t Central Device ${CENT_DEVICE}"
        echo -e "RUN -> \t${cmd_main}\n"
        eval "${cmd_main}"
        if [[ $i -eq 0 ]]
        then
            screen_name_list+=( "cent_${CENT_DEVICE}" )
            program_list+=( "tx_rx_zfc" )
        fi
    fi

    sleep 15

    # gracefully quit screens
    k=0
    for screen_name in "${screen_name_list[@]}"
    do
        if screen -list | grep -q "$screen_name"
        then
            session_id=$(screen -list | grep "$screen_name" | awk '{print $1}')
            echo "Screen '$screen_name' session id '$session_id' is ACTIVE."
            if screen -S "$session_id" -X stuff "pgrep ${program_list[k]}$(printf '\r')" > /dev/null 2>&1;
            then
                screen -X -S "$session_id" stuff "^C"
                echo "Closing program gracefully via '^C'"
                sleep 1
            fi
            if screen -S "$session_id" -X stuff "pgrep ${program_list[k]}$(printf '\r')" > /dev/null 2>&1
            then
                echo "Closing program gracefully -> Failed!"
                screen -X -S "$session_id" stuff "^X"
                echo "Closing program forcefully via '^X'"
                sleep 1
                # Kill the program with SIGKILL (kill -9)
                screen -S "$session_id" -X stuff "pkill -9 ${program_list[k]}$(printf '\r')"
                echo "Kill program forcefully via 'pkill -9 ${program_list[k]}'"
                sleep 1
            else
                echo "Closing program gracefully -> Success!"
            fi
        else
            echo "Screen '$screen_name' session id '$session_id' is INACTIVE."
        fi

        (( ++k ))

    done
done
