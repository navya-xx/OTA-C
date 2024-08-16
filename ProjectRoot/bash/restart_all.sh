#!/bin/bash

remote_nodes=("rpi4m1@192.168.5.241" "rpi4m2@192.168.5.242" "rpi4m3@192.168.5.243" "rpi4m4@192.168.5.244" "rpi4m5@192.168.5.245" "rpi4compute@192.168.5.246" "nuc01@192.168.5.248")

# Name of the tmux session
SESSION_NAME="restart"

tmux kill-session -t $SESSION_NAME

# Create a new tmux session, but don't attach to it yet
tmux new-session -d -s $SESSION_NAME

# Split the window into panes (2 rows, 3 columns)
tmux split-window -h -t $SESSION_NAME     # Split horizontally into 2 panes
tmux split-window -h -t $SESSION_NAME     # Split horizontally into 2 panes
tmux split-window -h -t $SESSION_NAME     # Split horizontally into 2 panes
tmux split-window -v -t $SESSION_NAME:0.0 # Split the first pane vertically
tmux split-window -v -t $SESSION_NAME:0.2 # Split the second pane vertically
tmux split-window -v -t $SESSION_NAME:0.4 # Split the second pane vertically
# tmux split-window -v -t $SESSION_NAME:0.6 # Split the second pane vertically

for (( i=0; i<${#remote_nodes[@]}; i++ ))
do
    node_name="${remote_nodes[$i]}"
    tmux send-keys -t $SESSION_NAME:0.$i  "ssh ${node_name} 'shutdown -r now'" C-m
done

sleep 10

for (( i=0; i<${#remote_nodes[@]}; i++ ))
do
    node_name="${remote_nodes[$i]}"
    tmux send-keys -t $SESSION_NAME:0.$i  "ssh ${node_name} 'uhd_find_devices'" C-m
done

tmux attach-session -t $SESSION_NAME