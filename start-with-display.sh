#!/bin/bash

# Start Virtual Display
export DISPLAY=:0
Xvfb :0 -screen 0 1920x1080x24 &

# Start Dolphin with arguments given to the script
/opt/Ishiiruka/build/Binaries/dolphin-emu "$@"