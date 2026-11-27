#!/bin/sh
# Each tag remembers its own layout. Changing the layout of one workspace
# must leave the other workspaces alone.
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
trap 'kill $P1 $P2 $P3 $P4 $DWM_PID $SETBG_PID $XVFB_PID 2>/dev/null || true' EXIT INT TERM
sleep 0.5
P1= P2= P3= P4=
xmessage -name st-256color -title one -geometry 400x300+50+80 one >/dev/null 2>&1 & P1=$!
xmessage -name firefox -title two -geometry 400x300+600+80 two >/dev/null 2>&1 & P2=$!
xmessage -name pcmanfm -title three -geometry 400x300+300+400 three >/dev/null 2>&1 & P3=$!
xmessage -name xterm -title four -geometry 400x300+700+400 four >/dev/null 2>&1 & P4=$!
sleep 0.8

mkdir -p /out
: >/out/taglayouts.txt
geom() {
	A=$(xdotool getactivewindow)
	W=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	echo "$1 active=$A ${W}x" >>/out/taglayouts.txt
}

fail() {
	echo "FAIL: $*" >>/out/taglayouts.txt
	import -window root "/out/taglayouts-fail.png" 2>/dev/null || true
	exit 1
}

# Move two of the four clients to tag 2, leaving two on each tag.
xdotool key --clearmodifiers super+shift+2
sleep 0.2
xdotool key --clearmodifiers super+shift+2
sleep 0.3

# Tag 1 is splith: the focused client shares the width with its neighbour.
xdotool key --clearmodifiers super+1
sleep 0.4
geom tag1-splith
SPLIT_W=$W

# Tag 2 becomes tabbed: the selected tab spans the whole work area.
xdotool key --clearmodifiers super+2
sleep 0.3
xdotool key --clearmodifiers super+w
sleep 0.4
geom tag2-tabbed
TAB_W=$W
[ "$TAB_W" -gt "$((SPLIT_W * 3 / 2))" ] || fail "tag2 tabbed width $TAB_W, tag1 splith width $SPLIT_W"

# Switching back must show tag 1 still in splith.
xdotool key --clearmodifiers super+1
sleep 0.4
geom tag1-again
[ "$W" -eq "$SPLIT_W" ] || fail "tag 1 leaked tag 2 layout: width $SPLIT_W -> $W"

# Tag 2 must still be tabbed.
xdotool key --clearmodifiers super+2
sleep 0.4
geom tag2-again
[ "$W" -eq "$TAB_W" ] || fail "tag 2 lost its layout: width $TAB_W -> $W"

# Make tag 1 tabbed, then set tag 2 back to splith (layout toggle split):
# neither change may leak.
xdotool key --clearmodifiers super+1
sleep 0.3
xdotool key --clearmodifiers super+w
sleep 0.4
geom tag1-tabbed
TAG1_TAB_W=$W
[ "$TAG1_TAB_W" -gt "$((SPLIT_W * 3 / 2))" ] || fail "tag1 tabbed width $TAG1_TAB_W not full"
xdotool key --clearmodifiers super+2
sleep 0.3
xdotool key --clearmodifiers super+e
sleep 0.4
geom tag2-splith
[ "$W" -lt "$((TAG1_TAB_W * 3 / 4))" ] || fail "tag2 splith width $W not tiled"
xdotool key --clearmodifiers super+1
sleep 0.4
geom tag1-tabbed-again
[ "$W" -eq "$TAG1_TAB_W" ] || fail "tag 1 leaked tag 2 layout: width $TAG1_TAB_W -> $W"

import -window root /out/taglayouts.png
xwininfo -root -tree >/out/taglayouts-tree.txt 2>&1
exit 0
