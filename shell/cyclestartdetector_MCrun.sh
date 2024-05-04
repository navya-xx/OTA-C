#!/bin/bash

LEAF_DEVICES_NUC=( "32C793E" "32C79BE" "32C7981" )
LEAF_DEVICES_NUC01=( "32B1708" "32B1728" "32B172B" )
LEAF_DEVICES_RPI4COMPUTE=( "32C79F7" )

LEAF_DEVICES_NUC_ID=( 41 73 109 )
LEAF_DEVICES_NUC01_ID=( 97 173 211 )
LEAF_DEVICES_RPI4COMPUTE_ID=( 139 )

LEAF_SSH_PREFIX_NUC01="ssh nuc01@192.168.5.68"
LEAF_SSH_PREFIX_RPI4COMPUTE="ssh rpi4compute@192.168.5.96"

LEAF_HOME_NUC="/home/nuc"
LEAF_HOME_NUC01="/home/nuc01"
LEAF_HOME_RPI4COMPUTE="/home/rpi4compute"

CENT_DEVICE_NUC01="32C7920"

LOGFOLDER="./LogFolder"

NUM_MC_RUNS=2

for (( i=0; i < $NUM_MC_RUNS; i++ ))
do
    echo -e "\n RUN $i \n"
    echo -e "\n DEVICE NUC\n"
    dev_counter=0
    for nuc_dev in "${LEAF_DEVICES_NUC[@]}"
    do
        cmd_quit="screen -S csdtest_nuc_${nuc_dev} -X quit"
        cmd_main="screen -L -Logfile ${LOGFOLDER}/logfile_${nuc_dev}_${i}.log -S csdtest_nuc_${nuc_dev} -d -m ${LEAF_HOME_NUC}/OTA-C/cpp/build/test_cyclestartdetector serial=${nuc_dev} ${LEAF_DEVICES_NUC_ID[$dev_counter]}"
        echo -e "\t Leaf Device ${LEAF_DEVICES_NUC_ID[$dev_counter]}"
        eval "${cmd_quit}"
        eval "${cmd_main}"
        echo -e "\t${cmd_quit}"
        echo -e "\t${cmd_main}\n"
        (( dev_counter++ ))
    done

    echo -e "\n DEVICE NUC01\n"
    dev_counter=0
    for nuc01_dev in "${LEAF_DEVICES_NUC01[@]}"
    do
        cmd_quit="screen -S csdtest_nuc01_${nuc01_dev} -X quit"
        cmd_main="screen -L -Logfile ${LOGFOLDER}/logfile_${nuc01_dev}_${i}.log -S csdtest_nuc01_${nuc01_dev} -d -m ${LEAF_SSH_PREFIX_NUC01} ${LEAF_HOME_NUC01}/OTA-C/cpp/build/test_cyclestartdetector serial=${nuc01_dev} ${LEAF_DEVICES_NUC01_ID[$dev_counter]}"
        echo -e "\t Leaf Device ${LEAF_DEVICES_NUC01_ID[$dev_counter]}"
        eval "${cmd_quit}"
        eval "${cmd_main}"
        echo -e "\t${cmd_quit}"
        echo -e "\t${cmd_main}\n"
        (( dev_counter++ ))
    done

    echo -e "\n DEVICE RPI4COMPUTE\n"
    dev_counter=0
    for rpi4compute_dev in "${LEAF_DEVICES_RPI4COMPUTE[@]}"
    do
        cmd_quit="screen -S csdtest_rpi4compute_${rpi4compute_dev} -X quit"
        cmd_main="screen -L -Logfile ${LOGFOLDER}/logfile_${rpi4compute_dev}_${i}.log -S csdtest_rpi4compute_${rpi4compute_dev} -d -m ${LEAF_SSH_PREFIX_RPI4COMPUTE} ${LEAF_HOME_RPI4COMPUTE}/OTA-C/cpp/build/test_cyclestartdetector serial=${rpi4compute_dev} ${LEAF_DEVICES_RPI4COMPUTE_ID[$dev_counter]}"
        echo -e "\t Leaf Device ${LEAF_DEVICES_RPI4COMPUTE_ID[$dev_counter]}"
        eval "${cmd_quit}"
        eval "${cmd_main}"
        echo -e "\t${cmd_quit}"
        echo -e "\t${cmd_main}\n"
        (( dev_counter++ ))
    done

    sleep 2

    echo -e "\n DEVICE CENT (NUC01)\n"
    cmd_quit="screen -S cent_nuc01_${CENT_DEVICE_NUC01}_run_${i} -X quit"
    cmd_main="screen -L -Logfile ${LOGFOLDER}/logfile_${CENT_DEVICE_NUC01}_${i}.log -S cent_nuc01_${CENT_DEVICE_NUC01}_run_${i} -d -m ${LEAF_SSH_PREFIX_NUC01} ${LEAF_HOME_NUC01}/OTA-C/cpp/build/tx_rx_zfc --args serial=${CENT_DEVICE_NUC01} --file /home/nuc01/OTA-C/cpp/storage/mc_run_${i}_data.dat --rate 1e6 --bw 2e6 --Tx-N-zfc 79 --Tx-R-zfc 10 --gain 60 --Rx-N-zfc 257"
    echo -e "\t Central Device ${LEAF_DEVICES_RPI4COMPUTE_ID[$dev_counter]}"
    eval "${cmd_quit}"
    eval "${cmd_main}"
    echo -e $cmd_quit
    echo -e $cmd_main

    sleep 10
done
