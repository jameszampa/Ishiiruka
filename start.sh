#!/bin/bash

# Set up virtual display for OpenGL support
export DISPLAY=:99
Xvfb :99 -screen 0 1920x1080x24 > /dev/null 2>&1 &

# Wait a moment for Xvfb to start
sleep 2

# Check if Xvfb is running
if ! pgrep -x "Xvfb" > /dev/null; then
    echo "Failed to start Xvfb virtual display"
    exit 1
fi

echo "Virtual display started on :99"

/opt/Ishiiruka/build/Binaries/dolphin-emu-nogui  -e /iso/SSBM.iso -d /outputs -o game -i /outputs/playback.json --cout