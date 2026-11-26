#!/bin/sh
# In-place restart keeps tags, per-tag layouts and factors, client order and
# focus; SIGHUP (dwm-restart) does the same; SIGTERM quits cleanly and hands
# the clients back to the root window. Exits non-zero on failure.
set -u

make -s clean all
cc -O2 -o /tmp/setbg test/setbg.c -lX11
export PATH="/src/integration:$PATH"

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
P1= P2= P3= P4=
trap 'kill $P1 $P2 $P3 $P4 $DWM_PID $SETBG_PID $XVFB_PID 2>/dev/null || true' EXIT INT TERM
sleep 0.5

mkdir -p /out
OUT=/out/restart.txt
: >"$OUT"
FAILS=0
pass() { echo "PASS $*" >>"$OUT"; }
fail() { echo "FAIL $*" >>"$OUT"; FAILS=$((FAILS + 1)); }
check() { # name expected actual
	if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: expected '$2', got '$3'"; fi
}
key() { xdotool key --clearmodifiers "$@"; sleep 0.25; }
desk() { xprop -id "$1" -notype _NET_WM_DESKTOP 2>/dev/null | sed -n 's/.* = //p'; }
curdesk() { xprop -root -notype _NET_CURRENT_DESKTOP | sed -n 's/.* = //p'; }
active() { xdotool getactivewindow 2>/dev/null; }
width() { xwininfo -id "$1" | awk '/Width:/ {print $2}'; }
height() { xwininfo -id "$1" | awk '/Height:/ {print $2}'; }
win() { xdotool search --name "^$1\$" | head -1; }

xmessage -name a -title one -geometry 300x200+50+80 one >/dev/null 2>&1 & P1=$!
sleep 0.3
xmessage -name b -title two -geometry 300x200+600+80 two >/dev/null 2>&1 & P2=$!
sleep 0.3
xmessage -name c -title three -geometry 300x200+300+400 three >/dev/null 2>&1 & P3=$!
sleep 0.3
xmessage -name d -title four -geometry 300x200+700+400 four >/dev/null 2>&1 & P4=$!
sleep 0.6
W1=$(win one) W2=$(win two) W3=$(win three) W4=$(win four)

check "ewmh number of desktops" 10 "$(xprop -root -notype _NET_NUMBER_OF_DESKTOPS | sed -n 's/.* = //p')"

# "four" (focused, newest) goes to tag 3; tag 3 becomes tabbed.
key super+shift+3
key super+3
key super+w
# Tag 1: grow the master area in resize mode, and give the focused stack
# client a larger share of its column.
key super+1
xdotool windowactivate "$W1" 2>/dev/null; sleep 0.3
key super+r
key Right Right Right
key Escape
xdotool windowactivate "$W2" 2>/dev/null; sleep 0.3
key super+r
key Down Down
key Escape
sleep 0.3

snapshot() {
	echo "$(desk "$W1") $(desk "$W2") $(desk "$W3") $(desk "$W4")|$(curdesk)|$(active)|$(width "$W1")x$(height "$W1") $(width "$W2")x$(height "$W2") $(width "$W3")x$(height "$W3")"
}
BEFORE=$(snapshot)
echo "before: $BEFORE" >>"$OUT"
check "four on tag 3" 2 "$(desk "$W4")"
check "focused two" "$W2" "$(active)"

# 1) keyboard restart: same pid, same state
key super+shift+r
sleep 1
check "restart keeps pid" yes "$(kill -0 $DWM_PID 2>/dev/null && echo yes || echo no)"
AFTER=$(snapshot)
echo "after key restart: $AFTER" >>"$OUT"
check "state after key restart" "$BEFORE" "$AFTER"

key super+3
TABW=$(width "$W4")
check "tag 3 still tabbed (full width)" yes "$([ "$TABW" -gt 1200 ] && echo yes || echo no)"
key super+1
sleep 0.3

# 2) SIGHUP through dwm-restart
dwm-restart
sleep 1
AFTER=$(snapshot)
echo "after SIGHUP: $AFTER" >>"$OUT"
check "state after dwm-restart" "$BEFORE" "$AFTER"

# 3) SIGTERM: clean exit, clients back on the root
kill -TERM "$DWM_PID"
sleep 0.8
check "SIGTERM stops dwm" no "$(kill -0 $DWM_PID 2>/dev/null && echo yes || echo no)"
PARENT=$(xwininfo -id "$W1" -tree | awk '/Parent window id:/ {print $4}')
ROOT=$(xwininfo -root | awk '/Window id:/ {print $4}')
check "client reparented to root" "$ROOT" "$PARENT"
check "tags kept for the next WM" 0 "$(desk "$W1")"

cat /tmp/dwm.log >>"$OUT"
echo "failures: $FAILS" >>"$OUT"
cat "$OUT"
[ "$FAILS" -eq 0 ]
