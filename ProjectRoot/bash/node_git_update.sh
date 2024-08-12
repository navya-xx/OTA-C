#!/bin/bash

remote_nodes=("rpi4m1@192.168.5.241" "rpi4m2@192.168.5.242" "rpi4m3@192.168.5.243" "rpi4m4@192.168.5.244" "rpi4m5@192.168.5.245" "rpi4compute@192.168.5.246" "nuc01@192.168.5.248")


# cmd="screen -dmS git_pull_n_make bash -c 'cd \$HOME/OTA-C/ProjectRoot/build/ && cmake ../ && make -j8 && echo \"Success\"'"

# echo $cmd

# screen -dmS git_pull_n_make bash -c "cd \$HOME/OTA-C/ProjectRoot/build/ && cmake ../ && make -j8 && echo \"Success\""

# Name of the tmux session
SESSION_NAME="GitMake"

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
    # cmd="ssh ${node_name} screen -dmS git_pull_n_make bash -c 'cd \$HOME/OTA-C && git stash && git pull && mkdir -p \$HOME/OTA-C/ProjectRoot/build/ && cd \$HOME/OTA-C/ProjectRoot/build/ && cmake ../ && make -j8 && echo \"Success\"'"
    # echo $cmd

    tmux send-keys -t $SESSION_NAME:0.$i  "ssh ${node_name} 'cd \$HOME/OTA-C && git stash && git pull && mkdir -p \$HOME/OTA-C/ProjectRoot/build/ && cd \$HOME/OTA-C/ProjectRoot/build/ && cmake ../ && make -j8 && echo \"Success\"'" C-m
    # echo "Git update on $node_name complete!"
    # eval "ssh ${node_name} 'cd \$HOME/OTA-C/cpp/build/; cmake ../; make -j4'"
done

tmux attach-session -t $SESSION_NAME