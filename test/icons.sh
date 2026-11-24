#!/bin/sh
# Real _NET_WM_ICON test in Docker/Xvfb.
#   16/32/64px sources, transparent corners, blank fallback, live update and
#   delete, taskbar + title bar + tabbed tab strip.
set -eu

make -s clean all
cc -O2 -o /tmp/iconwin test/iconwin.c -lX11
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
sleep 0.6

/tmp/iconwin red IconTest '#D00000' 64 >/tmp/iconwin-red.log 2>&1 &
RED_PID=$!
sleep 0.35
/tmp/iconwin green IconTest '#00A000' 32 >/tmp/iconwin-green.log 2>&1 &
GREEN_PID=$!
sleep 0.35
/tmp/iconwin blue IconTest '#0000D0' 16 >/tmp/iconwin-blue.log 2>&1 &
BLUE_PID=$!
sleep 0.5

mkdir -p /out
import -window root /out/icons-tile.png

# A client that never publishes an icon: the 16px slot must stay blank.
/tmp/iconwin blank IconTest '#555555' 0 >/tmp/iconwin-blank.log 2>&1 &
BLANK_PID=$!
sleep 0.6
import -window root /out/icons-blank.png

# Live property change after 2s, then deletion after 2s.
/tmp/iconwin update IconTest '#D00000' 32 2 change >/tmp/iconwin-update.log 2>&1 &
UPDATE_PID=$!
sleep 0.5
import -window root /out/icons-update-before.png
sleep 2.4
import -window root /out/icons-update-after.png

/tmp/iconwin gone IconTest '#0000D0' 32 2 delete >/tmp/iconwin-gone.log 2>&1 &
GONE_PID=$!
sleep 0.5
import -window root /out/icons-delete-before.png
sleep 2.4
import -window root /out/icons-delete-after.png

# Tabbed: every cell keeps its own icon; the active tab still switches.
xdotool key --clearmodifiers super+w
sleep 0.7
A=$(xdotool getactivewindow)
echo "tabs active=$A focus=$(xdotool getwindowfocus)" >/out/icons.txt
import -window root /out/icons-tabs.png
xdotool key --clearmodifiers super+Right
sleep 0.5
A=$(xdotool getactivewindow)
echo "tabs-right active=$A focus=$(xdotool getwindowfocus)" >>/out/icons.txt
import -window root /out/icons-tabs2.png
xwininfo -root -tree >/out/icons-tree.txt 2>&1

if ! kill -0 "$DWM_PID" 2>/dev/null; then
	echo "dwm exited:" >>/out/icons.txt
	cat /tmp/dwm.log >>/out/icons.txt
fi

kill "$RED_PID" "$GREEN_PID" "$BLUE_PID" "$BLANK_PID" "$UPDATE_PID" "$GONE_PID" \
	"$DWM_PID" "$SETBG_PID" "$XVFB_PID" 2>/dev/null || true
wait 2>/dev/null || true
