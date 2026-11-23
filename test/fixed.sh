#!/bin/sh
# Test a fixed-size GCR-like prompt in Docker/Xvfb.
#   FIXED_CLASS=Gcr-prompter  (rule floats it)
#   FIXED_CLASS=Test-fixed    (only the isfixed/hints path floats it)
set -eu

make -s clean all
cc -O2 -o /tmp/fixeddialog test/fixeddialog.c -lX11
cc -O2 -o /tmp/parent test/parent.c -lX11
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

CLASS="${FIXED_CLASS:-Gcr-prompter}"
/tmp/fixeddialog "$CLASS" >/tmp/fixed.log 2>&1 &
DIALOG_PID=$!
sleep 1.2

mkdir -p /out
CLIENT=$(xdotool search --class "^${CLASS}$" | head -1)
FRAME=$(/tmp/parent "$CLIENT")
CX=$(xwininfo -id "$CLIENT" | awk '/Absolute upper-left X/ {print $4}')
CY=$(xwininfo -id "$CLIENT" | awk '/Absolute upper-left Y/ {print $4}')
CW=$(xwininfo -id "$CLIENT" | awk '/Width:/ {print $2}')
CH=$(xwininfo -id "$CLIENT" | awk '/Height:/ {print $2}')
FX=$(xwininfo -id "$FRAME" | awk '/Absolute upper-left X/ {print $4}')
FY=$(xwininfo -id "$FRAME" | awk '/Absolute upper-left Y/ {print $4}')
FW=$(xwininfo -id "$FRAME" | awk '/Width:/ {print $2}')
FH=$(xwininfo -id "$FRAME" | awk '/Height:/ {print $2}')
FB=$(xwininfo -id "$FRAME" | awk '/Border width/ {print $3}')
# Expected: client 939x111, frame outer 949x141 at (208,323).
echo "$CLASS client=$CX,$CY ${CW}x${CH} frame=$FX,$FY ${FW}x${FH} bw=$FB" >/out/fixed.txt

# The WM must still respond after managing the fixed-size client.
xdotool key --clearmodifiers super+2
sleep 0.2
xdotool key --clearmodifiers super+1
sleep 0.2
import -window root /out/fixed-$CLASS.png
xwininfo -root -tree >/out/fixed-$CLASS-tree.txt 2>&1

kill "$DIALOG_PID" "$DWM_PID" "$SETBG_PID" "$XVFB_PID" 2>/dev/null || true
wait 2>/dev/null || true
