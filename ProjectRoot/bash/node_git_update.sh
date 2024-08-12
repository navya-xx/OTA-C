#!/bin/bash

remote_nodes=("rpi4m1@192.168.5.241" "rpi4m2@192.168.5.242" "rpi4m3@192.168.5.243" "rpi4m4@192.168.5.244" "rpi4m5@192.168.5.245" "rpi4compute@192.168.5.246" "nuc01@192.168.5.248")


cmd="screen -dmS git_pull_n_make bash -c 'cd \$HOME/OTA-C/ProjectRoot/build/ && cmake ../ && make -j8 && echo \"Success\"'"

echo $cmd

screen -dmS git_pull_n_make bash -c "cd \$HOME/OTA-C/ProjectRoot/build/ && cmake ../ && make -j8 && echo \"Success\""

for node_name in ${remote_nodes[@]}
do
    cmd="ssh ${node_name} screen -dmS git_pull_n_make bash -c 'cd \$HOME/OTA-C && git stash && git pull && mkdir -p \$HOME/OTA-C/ProjectRoot/build/ && cd \$HOME/OTA-C/ProjectRoot/build/ && cmake ../ && make -j8 && echo \"Success\"'"
    echo $cmd
    ssh "${node_name}" "screen -dmS git_pull_n_make bash -c 'cd \$HOME/OTA-C && git stash && git pull && mkdir -p \$HOME/OTA-C/ProjectRoot/build/ && cd \$HOME/OTA-C/ProjectRoot/build/ && cmake ../ && make -j8 && echo \"Success\"'"
    # echo "Git update on $node_name complete!"
    # eval "ssh ${node_name} 'cd \$HOME/OTA-C/cpp/build/; cmake ../; make -j4'"
done