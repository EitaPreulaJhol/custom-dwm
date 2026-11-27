#!/bin/sh
# i3 container tree: new windows open next to the focused one, split h/v,
# stacked/tabbed containers with their title strip, layout toggle split,
# directional focus and move through the tree, resize mode, the tree kept
# across a restart, and empty containers going away. Exits non-zero on
# failure.
set -u

make -s clean all || exit 1
cc -O2 -o /tmp/setbg test/setbg.c -lX11 || exit 1

export HOME=/tmp/treehome LANG=C.UTF-8 LC_ALL=C.UTF-8
rm -rf "$HOME"
mkdir -p "$HOME"

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
cd /src
./dwm >/tmp/dwm.log 2>&1 &
DWM_PID=$!
PIDS=
trap 'kill $PIDS $DWM_PID $SETBG_PID $XVFB_PID 2>/dev/null || true' EXIT INT TERM
sleep 0.5

mkdir -p /out
OUT=/out/tree.txt
: >"$OUT"
FAILS=0
pass() { echo "PASS $*" >>"$OUT"; }
fail() { echo "FAIL $*" >>"$OUT"; FAILS=$((FAILS + 1)); }
check() { # name expected actual
	if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: expected '$2', got '$3'"; fi
}
key() { xdotool key --clearmodifiers "$@"; sleep 0.3; }
# client geometry "x,y wxh" (the client window, inside its frame)
geom() {
	xwininfo -id "$1" | awk '/Absolute upper-left X/ {x=$4} /Absolute upper-left Y/ {y=$4}
		/Width:/ {w=$2} /Height:/ {h=$2} END {print x "," y " " w "x" h}'
}
shown() { xwininfo -id "$1" | awk '/Map State:/ {print $3}'; }
active() { xdotool getactivewindow 2>/dev/null; }
strips() { # mapped title strips: "x,y wxh" each
	for w in $(xdotool search --name '^dwm-tabs$' 2>/dev/null); do
		[ "$(shown "$w")" = IsViewable ] && geom "$w"
	done | sort | tr '\n' ';'
}
open() { # title -> window id
	xmessage -name "$1" -title "$1" -geometry 300x200+100+100 "$1" >/dev/null 2>&1 &
	PIDS="$PIDS $!"
	i=0
	while [ $i -lt 60 ]; do
		W=$(xdotool search --name "^$1\$" 2>/dev/null | head -1)
		[ -n "$W" ] && break
		sleep 0.05
		i=$((i + 1))
	done
	sleep 0.4
	echo "$W"
}
close() { xdotool windowclose "$1" 2>/dev/null || xkill -id "$1" >/dev/null 2>&1; sleep 0.4; }

# Work area: 1366x750 below the 18px bar, 10px outer gaps -> 10,28 1346x730.
# A frame has an 18px title bar above its client.

# 1. one window fills the workspace
A=$(open aaa)
check "A alone" "10,46 1346x712" "$(geom "$A")"

# 2. splith (default): B opens right of A
B=$(open bbb)
check "A left half" "10,46 668x712" "$(geom "$A")"
check "B right half" "688,46 668x712" "$(geom "$B")"

# 3. split v on B: C opens below B, in B's new container
key super+v
C=$(open ccc)
check "A keeps the left column" "10,46 668x712" "$(geom "$A")"
check "B top right" "688,46 668x342" "$(geom "$B")"
check "C bottom right" "688,416 668x342" "$(geom "$C")"

# 4. directional focus follows the tree
check "C focused" "$C" "$(active)"
key super+Up
check "Up: B" "$B" "$(active)"
key super+Down
key super+Left
check "Left: A" "$A" "$(active)"
key super+Right
check "Right: back to C (its container's focus)" "$C" "$(active)"

# 5. tabbed: B and C share the right column under one strip
key super+w
check "C shown as the tab" IsViewable "$(shown "$C")"
check "B hidden behind it" IsUnviewable "$(shown "$B")"
check "C below the strip, no own title" "688,46 668x712" "$(geom "$C")"
check "one strip over the column" "688,28 668x18;" "$(strips)"
key super+Left
check "Left in tabs: B" "$B" "$(active)"
check "B shown" IsViewable "$(shown "$B")"
check "C hidden" IsUnviewable "$(shown "$C")"
key super+Left
check "Left from the first tab: A" "$A" "$(active)"
check "B stays the shown tab" IsViewable "$(shown "$B")"

# 6. click on the C tab (right half of the strip)
xdotool mousemove 1200 36 click 1
sleep 0.4
check "tab click focuses C" "$C" "$(active)"
check "tab click shows C" IsViewable "$(shown "$C")"

# 7. stacked: one title row per window
key super+s
check "stacked strip has two rows" "688,28 668x36;" "$(strips)"
check "C under the rows" "688,64 668x694" "$(geom "$C")"

# 8. layout toggle split goes back to splitv
key super+e
check "no strip after toggle split" "" "$(strips)"
check "B back on top" "688,46 668x342" "$(geom "$B")"
check "C back below" "688,416 668x342" "$(geom "$C")"
key super+e
check "toggle split again: splith" "1027,46 329x712" "$(geom "$C")"
key super+e

# 9. move: A enters the right container, then leaves it again
key super+Left
check "A focused" "$A" "$(active)"
key super+shift+Right
check "A moved into the column: full width" "1346" "$(geom "$A" | sed 's/.* \([0-9]*\)x.*/\1/')"
check "A still focused" "$A" "$(active)"
key super+shift+Left
check "A back in the left column" "10,46 668x712" "$(geom "$A")"
check "B back on top right" "688,46 668x342" "$(geom "$B")"

# 10. move up out of a splith workspace: it becomes splitv
key super+shift+Up
check "A on top, full width" "10,46 1346x342" "$(geom "$A")"
check "B below A, full width" "10,416 1346x157" "$(geom "$B")"
key super+shift+Down
key super+shift+Left
check "A left again" "10,46 668x712" "$(geom "$A")"

# 11. resize mode: grow A's width by 2% of the row
key super+r
key Right
key Escape
check "A wider" "10,46 694x712" "$(geom "$A")"

# 12. the tree survives a restart
BEFORE="$(geom "$A")|$(geom "$B")|$(geom "$C")"
kill -HUP "$DWM_PID"
sleep 1.2
AFTER="$(geom "$A")|$(geom "$B")|$(geom "$C")"
check "geometry after restart" "$BEFORE" "$AFTER"
check "A focused after restart" "$A" "$(active)"
key super+Right
check "Right after restart: into the column" yes "$(case "$(active)" in "$B"|"$C") echo yes;; *) echo no;; esac)"
key super+w
check "tabbed after restart" "714,28 642x18;" "$(strips)"
key super+e

# 13. a new window opens after the focused one, in its container
xdotool windowactivate "$C" 2>/dev/null; sleep 0.3
check "C focused" "$C" "$(active)"
D=$(open ddd)
check "D below C in the column" "yes" "$([ "$(geom "$D" | cut -d, -f1)" = "$(geom "$C" | cut -d, -f1)" ] && echo yes || echo no)"

# 14. closing windows: the empty container goes away
close "$B"
close "$C"
close "$D"
check "A alone again" "10,46 1346x712" "$(geom "$A")"
E=$(open eee)
check "E next to A (splith)" "688,46 668x712" "$(geom "$E")"

# 15. layout on an empty workspace applies to the next windows
key super+2
key super+w
F=$(open fff)
check "F in a tabbed workspace" "10,28 1346x18;" "$(strips)"
key super+1

import -window root /out/tree.png 2>/dev/null || true
echo "failures: $FAILS" >>"$OUT"
cat "$OUT"
[ "$FAILS" -eq 0 ]
