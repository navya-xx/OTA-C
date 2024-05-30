#!/bin/bash

remote_nodes=("rpi4m1@192.168.5.105" "rpi4m2@192.168.5.106" "rpi4m3@192.168.5.104" "rpi4m4@192.168.5.108" "rpi4m5@192.168.5.107" "rpi4compute@192.168.5.109" "nuc01@192.168.5.68")


cmd="screen -dmS git_pull_n_make bash -c 'cd \$HOME/OTA-C/cpp/build/ && cmake ../ && make -j4 && echo \"Success\"'"

echo $cmd

screen -dmS git_pull_n_make bash -c "cd \$HOME/OTA-C/cpp/build/ && cmake ../ && make -j4 && echo \"Success\""

for node_name in ${remote_nodes[@]}
do
    cmd="ssh ${node_name} screen -dmS git_pull_n_make bash -c 'cd \$HOME/OTA-C && git stash && git pull && cd \$HOME/OTA-C/cpp/build/ && cmake ../ && make -j4 && echo \"Success\"'"
    echo $cmd
    ssh "${node_name}" "screen -dmS git_pull_n_make bash -c 'cd \$HOME/OTA-C && git stash && git pull && cd \$HOME/OTA-C/cpp/build/ && cmake ../ && make -j4 && echo \"Success\"'"
    # echo "Git update on $node_name complete!"
    # eval "ssh ${node_name} 'cd \$HOME/OTA-C/cpp/build/; cmake ../; make -j4'"
done