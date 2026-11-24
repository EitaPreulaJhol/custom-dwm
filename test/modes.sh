#!/bin/sh
# Resize/move binding modes in Docker/Xvfb.
# The mode keys are bare arrows, so this catches the regression where they
# were not grabbed on the root and went to the focused client instead.
set -eu

make -s clean all
cc -O2 -o /tmp/setbg test/setbg.c -lX11

DISPLAY_NUM="${DISPLAY_NUM:-99}"
export DISPLAY=":${DISPLAY_NUM}"
rm -f "/tmp/.X${DISPLAY_NUM}-lock" "/tmp/.X11-unix/X${DISPLAY_NUM}"
Xvfb ":${DISPLAY_NUM}" -screen 0 1366x768x24 -dpi 92 -nolisten tcp >/tmp/xvfb.log 2>&1 &
XVFB_PID=$!
i=0
while [ ! -S "/tmp/.X11-unix/X${DISPLAY_NUM}" ] && [ $i -lt 200 ]; do
	sleep 0.025
	i=$((i + 1))
done

/tmp/setbg '#888888' &
SETBG_PID=$!
./dwm >/tmp/dwm.log 2>&1 &
DWM_PID=$!
sleep 0.5
xmessage -name st-256color -title one -geometry 400x300+50+80 one >/dev/null 2>&1 & P1=$!
xmessage -name firefox -title two -geometry 400x300+600+80 two >/dev/null 2>&1 & P2=$!
xmessage -name pcmanfm -title three -geometry 400x300+300+400 three >/dev/null 2>&1 & P3=$!
sleep 0.8

mkdir -p /out
: >/out/modes.txt
geom() {
	A=$(xdotool getactivewindow)
	X=$(xwininfo -id "$A" | awk '/Absolute upper-left X/ {print $4}')
	Y=$(xwininfo -id "$A" | awk '/Absolute upper-left Y/ {print $4}')
	W=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	H=$(xwininfo -id "$A" | awk '/Height:/ {print $2}')
	echo "$1 active=$A $X,$Y ${W}x${H}" >>/out/modes.txt
}

geom before
xdotool key --clearmodifiers super+space
sleep 0.3
geom float
FLOAT_W=$W
FLOAT_X=$X
FLOAT_Y=$Y

xdotool key --clearmodifiers super+r
sleep 0.2
import -window root /out/modes-resize-bar.png
xdotool key --clearmodifiers Right
sleep 0.3
geom resize-right
[ "$W" -gt "$FLOAT_W" ] || { echo "resize Right did not grow width ($FLOAT_W -> $W)" >>/out/modes.txt; exit 1; }
xdotool key --clearmodifiers Return
sleep 0.2

xdotool key --clearmodifiers super+m
sleep 0.2
import -window root /out/modes-move-bar.png
xdotool key --clearmodifiers Right
sleep 0.3
geom move-right
[ "$X" -gt "$FLOAT_X" ] || { echo "move Right did not move x ($FLOAT_X -> $X)" >>/out/modes.txt; exit 1; }
xdotool key --clearmodifiers Down
sleep 0.3
geom move-down
[ "$Y" -gt "$FLOAT_Y" ] || { echo "move Down did not move y ($FLOAT_Y -> $Y)" >>/out/modes.txt; exit 1; }
xdotool key --clearmodifiers Return
sleep 0.2
import -window root /out/modes-floating.png

# Back to the tiled layout: resize mode changes mfact, move mode swaps cells.
xdotool key --clearmodifiers super+space
sleep 0.3
geom tiled
TILED_W=$W
xdotool key --clearmodifiers super+r
sleep 0.2
xdotool key --clearmodifiers Right
sleep 0.3
geom tiled-resize
[ "$W" -ne "$TILED_W" ] || { echo "tiled resize did not change width" >>/out/modes.txt; exit 1; }
xdotool key --clearmodifiers Return
sleep 0.2

xdotool key --clearmodifiers super+m
sleep 0.2
xdotool key --clearmodifiers Right
sleep 0.3
geom tiled-move
[ "$X" -ne 15 ] || { echo "tiled move did not change cell" >>/out/modes.txt; exit 1; }
xdotool key --clearmodifiers Return
sleep 0.2
import -window root /out/modes-tiled.png

# After Return the grabs must be released and normal Mod+direction focus works.
A_BEFORE=$(xdotool getactivewindow)
xdotool key --clearmodifiers super+Left
sleep 0.3
A_AFTER=$(xdotool getactivewindow)
[ "$A_BEFORE" != "$A_AFTER" ] || { echo "normal super+Left did not change focus" >>/out/modes.txt; exit 1; }
echo "normal-left $A_BEFORE -> $A_AFTER" >>/out/modes.txt

xwininfo -root -tree >/out/modes-tree.txt 2>&1
kill "$P1" "$P2" "$P3" "$DWM_PID" "$SETBG_PID" "$XVFB_PID" 2>/dev/null || true
wait 2>/dev/null || true
