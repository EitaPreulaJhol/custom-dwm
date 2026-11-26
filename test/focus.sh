#!/bin/sh
# Keyboard focus checks in Docker/Xvfb. Each check prints PASS/FAIL and the
# script exits non-zero if any failed. "Has the keyboard" is verified both
# with XGetInputFocus and by typing a key and reading the client's own log,
# with the pointer parked on the bar so PointerRoot fallbacks are exposed.
set -u

make -s clean all || exit 1
cc -O2 -o /tmp/focuswin test/focuswin.c -lX11 || exit 1
cc -O2 -o /tmp/setbg test/setbg.c -lX11 || exit 1

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
./dwm >/tmp/dwm.log 2>&1 &
DWM_PID=$!
sleep 0.6

LOG=/tmp/focus.log
: >"$LOG"
FAILS=0
PIDS=""

pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; FAILS=$((FAILS + 1)); }

spawn() { # NAME [options...]: start a client, print its window id
	/tmp/focuswin "$@" >>"$LOG" 2>&1 &
	PIDS="$PIDS $!"
	n=0
	while [ $n -lt 40 ]; do
		w=$(awk -v n="$1" '$1 == n && $2 == "window" {print $3}' "$LOG" | tail -1)
		[ -n "$w" ] && { echo "$w"; return; }
		sleep 0.05
		n=$((n + 1))
	done
}
win() { awk -v n="$1" '$1 == n && $2 == "window" {print $3}' "$LOG" | tail -1; }
key() { xdotool key --clearmodifiers "$1"; sleep 0.35; }
park() { xdotool mousemove 700 5; sleep 0.1; } # over the bar
focused() { printf '0x%x\n' "$(xdotool getwindowfocus 2>/dev/null || echo 0)"; }

# typed NAME TOKEN: does NAME (and only NAME) receive the key TOKEN?
typed() {
	before=$(wc -l <"$LOG")
	xdotool key "$2"
	sleep 0.3
	got=$(tail -n +"$((before + 1))" "$LOG" | awk '$2 == "key" {print $1}' | tr '\n' ' ')
	[ "$got" = "$1 " ]
}
check() { # LABEL NAME KEY: NAME must have the input focus and get KEY
	w=$(win "$2")
	f=$(focused)
	typed "$2" "$3"; ok=$?
	if [ "$f" = "$w" ] && [ $ok -eq 0 ]; then
		pass "$1"
	else
		fail "$1 (want $2=$w, focus=$f, typed to: ${got:-nobody})"
	fi
}
clientlist() { xprop -root _NET_CLIENT_LIST; }

park

# 1. Switching tags must hand the keyboard to the tag's focused client.
A=$(spawn A); sleep 0.5
check "new window gets the keyboard" A a
key super+2
B=$(spawn B); sleep 0.5
check "window on tag 2" B b
park
key super+1
check "back to tag 1: A has the keyboard" A c
key super+2
check "back to tag 2: B has the keyboard" B d
key super+Tab
check "Mod+Tab back_and_forth: A has the keyboard" A e

# 2. Moving the focused window away gives the keyboard to the next one.
C=$(spawn C); sleep 0.5
key super+shift+3
check "Mod+Shift+3 moves C away, A keeps the keyboard" A f
key super+3
check "tag 3: C has the keyboard" C g
key super+1

# 3. Tabbed: cycling and closing tabs keeps the keyboard on the visible tab.
key super+w
D=$(spawn D); sleep 0.5
check "tabbed: new tab D" D h
key super+Left
check "tabbed: Mod+Left to A" A i
key super+Right
check "tabbed: Mod+Right to D" D j
DPID=$(echo $PIDS | awk '{print $NF}')
xdotool key super+q; sleep 0.1; kill "$DPID" 2>/dev/null; sleep 0.5
check "tabbed: closing D focuses A" A k
key super+h

# 4. A client that maps and withdraws right away must not leave a ghost
#    frame that holds the focus.
E=$(spawn E -u 150); sleep 1.2
if clientlist | grep -q "$E"; then
	fail "short-lived E still managed after withdrawing ($(clientlist))"
else
	pass "short-lived E unmanaged"
fi
check "after E withdrew, A has the keyboard" A l

# 5. Clicking an unfocused client focuses it.
key super+4
G=$(spawn G); sleep 0.4
H=$(spawn H); sleep 0.5
GX=$(xwininfo -id "$G" | awk '/Absolute upper-left X/ {print $4}')
GY=$(xwininfo -id "$G" | awk '/Absolute upper-left Y/ {print $4}')
xdotool mousemove $((GX + 50)) $((GY + 50)) click 1; sleep 0.3
park
check "click focuses G" G m

# 6. Mod+Return only spawns the terminal; it must not reorder the tiles.
before=$(xwininfo -id "$G" | awk '/Absolute upper-left X/ {print $4}')
key super+Return
after=$(xwininfo -id "$G" | awk '/Absolute upper-left X/ {print $4}')
[ "$before" = "$after" ] && pass "Mod+Return does not zoom" \
	|| fail "Mod+Return moved G from x=$before to x=$after (zoom)"

# 7. A window that asks for fullscreen before mapping covers the monitor
#    and has the keyboard.
key super+5
F=$(spawn F -f); sleep 0.6
geo=$(xwininfo -id "$F" | awk '/Absolute upper-left X/ {x=$4} /Absolute upper-left Y/ {y=$4} /Width:/ {w=$2} /Height:/ {h=$2} END {print x","y" "w"x"h}')
[ "$geo" = "0,0 1366x768" ] && pass "fullscreen-at-map geometry" \
	|| fail "fullscreen-at-map geometry is $geo, want 0,0 1366x768"
check "fullscreen-at-map F has the keyboard" F n

# 8. _NET_ACTIVE_WINDOW (i3 focus_on_window_activation smart): a visible
#    window is focused; one on another tag only becomes urgent.
key super+4
xdotool windowactivate --sync "$H" >/dev/null 2>&1 &
sleep 0.5
check "_NET_ACTIVE_WINDOW focuses visible H" H o
xdotool windowactivate "$A" >/dev/null 2>&1 &
sleep 0.5
check "_NET_ACTIVE_WINDOW on hidden A keeps H" H p

mkdir -p /out
import -window root /out/focus-end.png 2>/dev/null || true
cp /tmp/dwm.log /out/focus-dwm.log 2>/dev/null || true
kill $PIDS "$DWM_PID" "$XVFB_PID" 2>/dev/null || true
echo "failures: $FAILS"
[ "$FAILS" -eq 0 ]
