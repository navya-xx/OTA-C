import os
import subprocess
import json

# List of scripts and their parameters
command_template = "ssh {node_username}@{node_address} 'cd $HOME/OTA-C && git stash && git checkout main && git pull && mkdir -p $HOME/OTA-C/ProjectRoot/build/ && cd $HOME/OTA-C/ProjectRoot/build/ && cmake ../ && make -j && ./otac_cent leaf {node_serial}'"
cent_template = "cd $HOME/OTA-C/ProjectRoot/build/ && ./otac_cent cent {node_serial}"
# command_template = "echo \"ssh {node_username}@{node_address}\""

# Name of the tmux session
session_name = "node_git_update"

def read_devices_config():
    filename = "../config/devices.json"
    with open(filename, 'r') as file:
        data = json.load(file)

    return data

def run_parallel_scripts():
    # Start a new tmux session
    subprocess.run(["tmux", "kill-session", "-t", session_name])
    subprocess.run(["tmux", "new-session", "-d", "-s", session_name])
    devices_config = read_devices_config()
    i = 0
    for key, val in devices_config.items():
        if (not key.startswith("32")):
            continue

        # For the first script, we're already in the first pane of the session.
        if i == 0:
            if (val["type"] == "leaf"):
                tmux_command = command_template.format(node_username=val["hostname"], node_address=val["IP"], node_serial=key)
                subprocess.run(["tmux", "send-keys", "-t", f"{session_name}:0", tmux_command, "C-m"])
            elif (val["type"] == "cent"):
                tmux_command = cent_template.format(node_serial=key)
                subprocess.run(["tmux", "send-keys", "-t", f"{session_name}:0", tmux_command, "C-m"])
        else:
            # Create a new horizontal split for each new script
            subprocess.run(["tmux", "split-window", "-h", "-t", f"{session_name}"])

            # Adjust the layout to evenly distribute panes
            subprocess.run(["tmux", "select-layout", "-t", f"{session_name}", "tiled"])

            if (val["type"] == "leaf"):
                tmux_command = command_template.format(node_username=val["hostname"], node_address=val["IP"], node_serial=key)
                subprocess.run(["tmux", "send-keys", "-t", f"{session_name}:0.{i}", tmux_command, "C-m"])
            elif (val["type"] == "cent"):
                tmux_command = cent_template.format(node_serial=key)
                subprocess.run(["tmux", "send-keys", "-t", f"{session_name}:0.{i}", tmux_command, "C-m"])

        i += 1

    # Attach to the session
    subprocess.run(["tmux", "attach-session", "-t", session_name])

if __name__ == "__main__":
    run_parallel_scripts()