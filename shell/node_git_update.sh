#!/bin/bash

remote_nodes=("ssh rpi4m1@192.168.5.105" "ssh rpi4m2@192.168.5.106" "ssh rpi4m3@192.168.5.104" "ssh rpi4m4@192.168.5.108" "ssh rpi4m5@192.168.5.107" "ssh rpi4compute@192.168.5.109" "ssh nuc01@192.168.5.68")

for node_ssh in ${remote_nodes[@]}
do
    cmd="${node_ssh} screen -dmS git_pull_n_make bash -c 'cd \$HOME/OTA-C && git stash && git pull && cd \$HOME/OTA-C/cpp/build/ && cmake ../ && make -j4 && echo \"Success\"'"
    echo $cmd
    eval $cmd
    # echo "Git update on $node_name complete!"
    # eval "ssh ${node_name} 'cd \$HOME/OTA-C/cpp/build/; cmake ../; make -j4'"
done