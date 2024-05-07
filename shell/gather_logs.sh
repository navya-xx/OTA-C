#!/bin/bash

remote_nodes=("rpi4m1@192.168.5.105" "rpi4m2@192.168.5.106" "rpi4m3@192.168.5.104" "rpi4m4@192.168.5.108" "rpi4m5@192.168.5.107" "rpi4compute@192.168.5.109" "nuc01@192.168.5.68")

for node_name in ${remote_nodes[@]}
do
    cmd_gather="scp ${node_name}:~/OTA-C/shell/LogFolder/* $HOME/OTA-C/shell/LogFolder"
    echo $cmd_gather
    eval $cmd_gather
    echo "Log data gathered from $node_name!"
    del_logs="ssh ${node_name} 'rm -rf \$HOME/OTA-C/shell/LogFolder/*'"
    echo $del_logs
    eval $del_logs
    echo "Log files deleted from $node_name!"
done