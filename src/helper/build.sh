#!/bin/bash
# Builds NFS11_SingleRace.asi (32-bit) with MinGW-w64. Usage: build.sh <imgui dir> <output dir>
# Dear ImGui 1.91.5 (MIT) with its Win32 and DirectX 9 backends.
set -e
cd "$(dirname "$0")"
IMGUI=$(cd "$1" && pwd); B=$(mkdir -p "$2" && cd "$2" && pwd)
CXX=i686-w64-mingw32-g++
FLAGS="-I$IMGUI -I$IMGUI/backends -DIMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS -DIMGUI_DISABLE_FILE_FUNCTIONS -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD -DIMGUI_DISABLE_DEBUG_TOOLS -std=c++17 -O2 -DNDEBUG"
OBJS=""
for s in NFS11_SingleRace.cpp SR_Overlay9.cpp; do o=$B/${s%.cpp}.o; $CXX $FLAGS -Wall -c $s -o $o; OBJS="$OBJS $o"; done
for s in $IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp $IMGUI/backends/imgui_impl_dx9.cpp $IMGUI/backends/imgui_impl_win32.cpp; do
  o=$B/imgui_$(basename ${s%.cpp}).o; [ -f $o ] || $CXX $FLAGS -c $s -o $o; OBJS="$OBJS $o"; done
$CXX -shared -static -static-libgcc -static-libstdc++ -s -o $B/NFS11_SingleRace.asi $OBJS -ldwmapi -lgdi32 -luser32
ls -l $B/NFS11_SingleRace.asi
