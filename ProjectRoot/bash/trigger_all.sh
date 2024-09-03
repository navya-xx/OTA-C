#!/bin/bash

node_serials=("32B1728" "32B172B" "32C793E" "32B1708" "337D42D" "32C7920" "32C79BE" "32C79C6" "32C7981" "32C79F7")
# leaf_node_serials=("32C79C6")

remote_nodes=("" "rpi4m1@192.168.5.241" "rpi4m2@192.168.5.242" "rpi4m3@192.168.5.243" "rpi4m4@192.168.5.244" "rpi4m5@192.168.5.245" "rpi4compute@192.168.5.246" "nuc01@192.168.5.248" "nuc01@192.168.5.248" "")
# remote_nodes=("nuc01@192.168.5.248")


# Name of the tmux session
SESSION_NAME="Trigger"

tmux kill-session -t $SESSION_NAME

# Create a new tmux session, but don't attach to it yet
tmux new-session -d -s $SESSION_NAME

# Split the window into panes (2 rows, 3 columns)
tmux split-window -h -t $SESSION_NAME     # Split horizontally into 2 panes
tmux split-window -h -t $SESSION_NAME     # Split horizontally into 2 panes
tmux split-window -h -t $SESSION_NAME     # Split horizontally into 2 panes
tmux split-window -h -t $SESSION_NAME     # Split horizontally into 2 panes
tmux split-window -v -t $SESSION_NAME:0.0 # Split the first pane vertically
tmux split-window -v -t $SESSION_NAME:0.2 # Split the second pane vertically
tmux split-window -v -t $SESSION_NAME:0.4 # Split the second pane vertically
tmux split-window -v -t $SESSION_NAME:0.6 # Split the second pane vertically
tmux split-window -v -t $SESSION_NAME:0.8 # Split the second pane vertically

timeout=900  # Set your timeout in seconds

for (( i=0; i<${#remote_nodes[@]}; i++ ))
do

    node_name="${remote_nodes[$i]}"
    node_serial="${node_serials[$i]}"

    echo "Iteration $i for node ${node_serial}: Starting..."

    # Start two commands in detached screen sessions
    if [ "${node_name}" == "" ]; then
        tmux send-keys -t $SESSION_NAME:0.$i "cd $HOME/OTA-C/ProjectRoot/build && ./CA_leaf ${node_serial} 1" C-m
    else
        tmux send-keys -t $SESSION_NAME:0.$i "ssh ${node_name} 'cd \$HOME/OTA-C/ProjectRoot/build && ./CA_leaf ${node_serial} 1'" C-m
    fi
done

tmux attach-session -t $SESSION_NAME

echo "Finished!"