#!/bin/bash

leaf_node_serials=("32B172B" "32C793E" "32B1708" "32C7981" "337D42D" "32C79F7" "32C7920" "32C79BE" "32C79C6")

remote_nodes=("rpi4m1@192.168.5.241" "rpi4m2@192.168.5.242" "rpi4m3@192.168.5.243" "rpi4m4@192.168.5.244" "rpi4m4@192.168.5.244" "rpi4m5@192.168.5.245" "rpi4m5@192.168.5.245" "rpi4compute@192.168.5.246" "nuc01@192.168.5.248")

leaf_node_ids=(23 37 53 89 113 151 197 211 241)

# Name of the tmux session
SESSION_NAME="mysession"

tmux kill-session -t $SESSION_NAME

# Create a new tmux session, but don't attach to it yet
tmux new-session -d -s $SESSION_NAME

# Split the window into panes (2 rows, 3 columns)
tmux split-window -h -t $SESSION_NAME     # Split horizontally into 2 panes
tmux split-window -h -t $SESSION_NAME     # Split horizontally into 2 panes
# tmux split-window -h -t $SESSION_NAME     # Split horizontally into 2 panes
tmux split-window -v -t $SESSION_NAME:0.0 # Split the first pane vertically
tmux split-window -v -t $SESSION_NAME:0.1 # Split the second pane vertically
tmux split-window -v -t $SESSION_NAME:0.3 # Split the second pane vertically
tmux split-window -v -t $SESSION_NAME:0.4 # Split the second pane vertically
tmux split-window -v -t $SESSION_NAME:0.6 # Split the second pane vertically
tmux split-window -v -t $SESSION_NAME:0.7 # Split the second pane vertically

for (( i=0; i<${#remote_nodes[@]}; i++ ))
do
    node_name="${remote_nodes[$i]}"
    node_serial="${leaf_node_serials[$i]}"
    node_id="${leaf_node_ids[$i]}"
    # command_pre="mv \$HOME/OTA-C/ProjectRoot/storage \$HOME/OTA-C/ProjectRoot/bkp_storage && mkdir -p \$HOME/OTA-C/ProjectRoot/storage/logs"
    # command_pre="sleep 1 && pkill -f CA_leaf && sleep 1"
    # tmux send-keys -t $SESSION_NAME:0.$i "ssh ${node_name} '${command_pre}'" C-m
    # sleep 3

    # command_run="mkdir -p \$HOME/OTA-C/ProjectRoot/storage/logs/ && cd \$HOME/OTA-C/ProjectRoot/build/ && gdb --ex run --args ./CA_leaf ${node_serial} ${node_id} | tee \$HOME/OTA-C/ProjectRoot/storage/logs/leaf_output.log"
    
    # command_run="mkdir -p \$HOME/OTA-C/ProjectRoot/storage/logs/ && cd \$HOME/OTA-C/ProjectRoot/build/ && ./CA_leaf ${node_serial} ${node_id} | tee \$HOME/OTA-C/ProjectRoot/storage/logs/leaf_output.log"

    tmux send-keys -t $SESSION_NAME:0.$i "ssh ${node_name} 'bash \$HOME/OTA-C/ProjectRoot/bash/CA_leaf_run.sh'" C-m

done

tmux attach-session -t $SESSION_NAME

# sleep 3

# cent_node_serial="32C79F7"
# cd $HOME/OTA-C/ProjectRoot/build/ 
# ./CA_cent_test $cent_node_serial JointTest | tee ../storage/logs/cent_output.log
# cmd="cd \$HOME/OTA-C/ProjectRoot/build/ && ./CA_cent_test ${cent_node_serial} JointTest | tee \$HOME/OTA-C/ProjectRoot/storage/logs/cent_output.log"
# eval $cmd
# echo $cmd