#!/bin/bash

echo "New hook ran:  $(date)" >> /tmp/tmux-hooks.log
echo -e "\t$@" >> /tmp/tmux-hooks.log
notify-send "Ran hook!"
