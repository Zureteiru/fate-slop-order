#!/bin/bash
# PortMaster launcher for Fate/Slop Order (internally: vnreader), an FGO story reader.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/vnreader"
cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# The game reads the controller itself through SDL; gptokeyb is only there for the exit hotkey.
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
$ESUDO chmod +x "$GAMEDIR/vnreader.${DEVICE_ARCH}"

$GPTOKEYB "vnreader.${DEVICE_ARCH}" &
pm_platform_helper "$GAMEDIR/vnreader.${DEVICE_ARCH}"
./vnreader.${DEVICE_ARCH} --fullscreen

pm_finish
