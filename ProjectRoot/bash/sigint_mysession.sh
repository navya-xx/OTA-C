#!/bin/bash
# Script to send Ctrl+C to all panes in all windows of the current session

tmux list-windows -F '#I' | while read window_id; do
    tmux list-panes -t $window_id -F '#P' | while read pane_id; do
        tmux send-keys -t $window_id.$pane_id C-c
    done
done