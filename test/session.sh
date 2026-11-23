#!/bin/sh
# End-to-end session test: dwm-session + autostart + restart + exit dialog.
# Runs inside dwm-test:local with the repository at /src.
set -eu

export HOME=/tmp/dwmhome
rm -rf "$HOME"
mkdir -p "$HOME"
cd /src
./integration/install.sh >/tmp/install.log 2>&1
export PATH="$HOME/.local/bin:/src:$PATH"

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

dwm-session >/tmp/session.log 2>&1 &
SESSION_PID=$!
sleep 1.2

xmessage -name 'firefox' -title 'Mozilla Firefox' -geometry 500x300+50+50 \
	'hello' >/dev/null 2>&1 &
CLIENT_PID=$!
sleep 0.8

mkdir -p /out
import -window root /out/session1.png
DWM_PID1=$(pgrep -x dwm || true)
echo "dwm pid before restart: ${DWM_PID1:-none}" >/out/session.txt

# $mod+Shift+r restarts dwm; the X client and the helpers must survive.
xdotool key --clearmodifiers super+shift+r
sleep 1.2
DWM_PID2=$(pgrep -x dwm || true)
echo "dwm pid after restart: ${DWM_PID2:-none}" >>/out/session.txt
import -window root /out/session2.png

# $mod+Shift+e opens the floating confirmation dialog.
xdotool key --clearmodifiers super+shift+e
sleep 0.8
import -window root /out/exit-dialog.png
xwininfo -root -tree >/out/session-tree.txt 2>&1

# Click the "Yes" button (the default is "No"). xdotool reports the client
# position with a title-bar offset, so use xwininfo's absolute coordinates.
DLG=$(xdotool search --name '^dwm-exit$' | head -1)
DX=$(xwininfo -id "$DLG" | awk '/Absolute upper-left X/ {print $4}')
DY=$(xwininfo -id "$DLG" | awk '/Absolute upper-left Y/ {print $4}')
xdotool mousemove $((DX + 17)) $((DY + 37)) click 1
sleep 1

if pgrep -x dwm >/dev/null; then
	echo "exit: dwm still running" >>/out/session.txt
else
	echo "exit: dwm stopped" >>/out/session.txt
fi
if kill -0 "$SESSION_PID" 2>/dev/null; then
	echo "exit: dwm-session still running" >>/out/session.txt
else
	echo "exit: dwm-session stopped" >>/out/session.txt
fi
[ -f "$HOME/.dwm/exit" ] && echo "exit: flag still present" >>/out/session.txt \
	|| echo "exit: flag cleaned" >>/out/session.txt

kill "$CLIENT_PID" "$SETBG_PID" "$XVFB_PID" 2>/dev/null || true
kill "$SESSION_PID" 2>/dev/null || true
pkill -x dwm 2>/dev/null || true
pkill -x i3-focus-outline 2>/dev/null || true
pkill -x dwm-clock 2>/dev/null || true
cat /tmp/session.log >&2
