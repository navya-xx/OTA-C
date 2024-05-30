#!/bin/bash

remote_nodes=("rpi4m1@192.168.5.105" "rpi4m2@192.168.5.106" "rpi4m3@192.168.5.104" "rpi4m4@192.168.5.108" "rpi4m5@192.168.5.107" "rpi4compute@192.168.5.109" "nuc01@192.168.5.68" "")

leaf_node_serials=("32B172B" "32C793E" "32B1708" "32C7981" "32C7920" "32C79BE" "32B1728" "32C79F7")
leaf_node_ids=(29 53 89 113 151 197 211 241)
cent_node_serial="32C79C6"

for (( i=0; i<${#remote_nodes[@]}; i++ ))
do
    node_name="${remote_nodes[$i]}"
    node_serial="${leaf_node_serials[$i]}"
    node_id="${leaf_node_ids[$i]}"

    if [ "$node_name" == "" ]
    then
        cmd="screen -dm -S leaf_test bash -c 'cd \$HOME/OTA-C/cpp/build/ && ./test_sync_leaf ${node_serial} ${node_id} | tee \$HOME/OTA-C/shell/LogFolder/leaf_output.log'"
        # screen -dm -S leaf_test bash -c 'cd \$HOME/OTA-C/cpp/build/ && ./test_sync_leaf ${node_serial} ${node_id} | tee \$HOME/OTA-C/shell/LogFolder/leaf_output.log'
        eval $cmd
    else
        cmd="ssh ${node_name} \"screen -dm -S leaf_test bash -c 'cd \$HOME/OTA-C/cpp/build/ && ./test_sync_leaf ${node_serial} ${node_id} | tee \$HOME/OTA-C/shell/LogFolder/leaf_output.log'\""
        ssh ${node_name} "screen -dm -S leaf_test bash -c 'cd \$HOME/OTA-C/cpp/build/ && ./test_sync_leaf ${node_serial} ${node_id} | tee \$HOME/OTA-C/shell/LogFolder/leaf_output.log'"
        # eval $cmd
    fi

    echo $cmd
done

sleep 3

cmd="screen -dm -S cent_test bash -c 'cd \$HOME/OTA-C/cpp/build/ && ./test_sync_cent ${cent_node_serial} JointTest2 | tee \$HOME/OTA-C/shell/LogFolder/cent_output.log'"
# screen -dm -S cent_test bash -c 'cd \$HOME/OTA-C/cpp/build/ && ./test_sync_cent ${cent_node_serial} JointTest | tee \$HOME/OTA-C/shell/LogFolder/cent_output.log'
eval $cmd
echo $cmd