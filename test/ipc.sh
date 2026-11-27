#!/bin/sh
# IPC socket and dwm-msg (queries, commands, errors, several commands per
# line, exec, subscribe, restart), the urgent task label and Mod+u, and the
# task overview (Mod+o). Exits non-zero on failure.
set -u

make -s clean all || exit 1
cc -O2 -o /tmp/setbg test/setbg.c -lX11 || exit 1
cc -O2 -o /tmp/clientmsg test/clientmsg.c -lX11 || exit 1

export HOME=/tmp/ipchome LANG=C.UTF-8 LC_ALL=C.UTF-8
export XDG_RUNTIME_DIR=/tmp/ipcrun
rm -rf "$HOME" "$XDG_RUNTIME_DIR"
mkdir -p "$HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
unset DWM_SOCKET

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
OUT=/out/ipc.txt
: >"$OUT"
FAILS=0
pass() { echo "PASS $*" >>"$OUT"; }
fail() { echo "FAIL $*" >>"$OUT"; FAILS=$((FAILS + 1)); }
check() { # name expected actual
	if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: expected '$2', got '$3'"; fi
}
has() { # name needle haystack
	case "$3" in *"$2"*) pass "$1" ;; *) fail "$1: no '$2' in '$3'" ;; esac
}
key() { xdotool key --clearmodifiers "$@"; sleep 0.3; }
msg() { ./dwm-msg "$@"; }
rootprop() { xprop -root -notype "$1" | sed -n 's/.* = //p'; }
curdesk() { rootprop _NET_CURRENT_DESKTOP; }
active() { xdotool getactivewindow 2>/dev/null; }
shown() { xwininfo -id "$1" | awk '/Map State:/ {print $3}'; }
desk() { xprop -id "$1" -notype _NET_WM_DESKTOP 2>/dev/null | sed -n 's/.* = //p'; }
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
pixel() { import -window root -crop 1x1+$1+$2 -depth 8 txt:- 2>/dev/null | sed -n 's/.*\(#[0-9A-Fa-f]\{6\}\).*/\1/p' | head -1; }

# 1. the socket: on the root window, under $XDG_RUNTIME_DIR, private
SOCK=$(xprop -root _DWM_IPC_SOCKET | sed -n 's/.* = "\(.*\)"/\1/p')
check "socket path published" "$XDG_RUNTIME_DIR/dwm-99.sock" "$SOCK"
check "socket mode" "srw-------" "$(ls -l "$SOCK" | cut -c1-10)"

# 2. queries
has "get_tasks" '[{"num":1,"name":"main","focused":true,"urgent":false,"windows":0}]' "$(msg get_tasks)"
has "get_tags: tag 1 focused" '{"num":1,"name":"1","focused":true' "$(msg get_tags)"
A=$(open aaa)
has "get_focused" "\"id\":$A,\"title\":\"aaa\"" "$(msg get_focused)"
has "get_windows" '"class":"Xmessage","instance":"aaa"' "$(msg get_windows)"
B=$(open bbb)
has "get_tree: splith with both" "{\"layout\":\"splith\",\"fact\":1.000,\"nodes\":[{\"id\":$A" "$(msg get_tree)"

# 3. commands and errors
check "split v" '{"success":true}' "$(msg split v)"
C=$(open ccc)
has "C in a splitv container" "\"layout\":\"splitv\"" "$(msg get_tree)"
check "layout tabbed" '{"success":true}' "$(msg layout tabbed)"
has "tabbed container" "\"layout\":\"tabbed\"" "$(msg get_tree)"
msg layout toggle split >/dev/null
has "toggle split back" "\"layout\":\"splitv\"" "$(msg get_tree)"
msg focus left >/dev/null
check "focus left" "$A" "$(active)"
check "unknown command" '{"success":false,"error":"unknown command"}' "$(msg frobnicate)"
msg frobnicate >/dev/null; check "dwm-msg exits 1 on failure" 1 "$?"
check "bad tag" '{"success":false,"error":"no such tag"}' "$(msg view 11)"
check "two commands on one line" '{"success":true}' "$(msg 'view 3; view 4')"
check "both ran" 3 "$(curdesk)"
msg view 1 >/dev/null
msg exec xmessage -name viaipc -title viaipc 'from; ipc' >/dev/null 2>&1
sleep 0.8
check "exec (with ';' in the command)" yes "$(xdotool search --name '^viaipc$' >/dev/null && echo yes || echo no)"
VIA=$(xdotool search --name '^viaipc$' | head -1)
PIDS="$PIDS $(xdotool getwindowpid "$VIA" 2>/dev/null)"

# 4. tasks through IPC
check "newtask with a label" '{"success":true}' "$(msg newtask my work)"
has "new task active" '{"num":2,"name":"my work","focused":true' "$(msg get_tasks)"
check "task by number" '{"success":true}' "$(msg task 1)"
check "back in main" 0 "$(curdesk)"
check "task by name" '{"success":true}' "$(msg task my work)"
check "in my work" 10 "$(curdesk)"
msg task 1 >/dev/null

# 5. events
msg subscribe focus tag window task >/tmp/events.txt 2>&1 &
SUBPID=$!
PIDS="$PIDS $SUBPID"
sleep 0.3
D=$(open ddd)
msg view 2 >/dev/null
msg task 2 >/dev/null
msg task 1 >/dev/null
msg view 1 >/dev/null
sleep 0.3
EV=$(cat /tmp/events.txt)
echo "$EV" >>"$OUT"
has "subscribe reply" '{"success":true}' "$(head -1 /tmp/events.txt)"
has "new window event" "{\"event\":\"window\",\"change\":\"new\",\"window\":{\"id\":$D" "$EV"
has "focus event" "{\"event\":\"focus\",\"window\":{\"id\":$D" "$EV"
has "tag event" '{"event":"tag","tags":[{"num":1,"name":"1","focused":false' "$EV"
has "task event" '{"event":"task","task":[{"num":1,"name":"main","focused":false' "$EV"
xdotool windowclose "$D" 2>/dev/null; sleep 0.5
has "close event" "{\"event\":\"window\",\"change\":\"close\",\"window\":{\"id\":$D" "$(cat /tmp/events.txt)"

# 6. restart: a new socket, dwm-msg still works
kill -HUP "$DWM_PID"
sleep 1.2
has "get_tasks after restart" '"name":"my work"' "$(msg get_tasks)"
check "subscriber disconnected by the restart" gone "$(kill -0 $SUBPID 2>/dev/null && echo alive || echo gone)"

# 7. urgent window in another task: its label is marked, Mod+u goes there
msg task 2 >/dev/null
U=$(open urg)
msg task 1 >/dev/null
/tmp/clientmsg "$U" _NET_WM_STATE 1 _NET_WM_STATE_DEMANDS_ATTENTION; sleep 0.4
has "task 2 urgent" '{"num":2,"name":"my work","focused":false,"urgent":true' "$(msg get_tasks)"
# the label cell of task 2 is right of "main" in the bar: look for the
# urgent background there
URGPX=no
for x in $(seq 40 4 140); do
	[ "$(pixel "$x" 3)" = "#AAAAAA" ] && URGPX=yes && break
done
check "urgent task label drawn" yes "$URGPX"
key super+u
check "Mod+u focuses the urgent window" "$U" "$(active)"
check "Mod+u switched to its task" 10 "$(curdesk)"
has "no longer urgent" '"name":"my work","focused":true,"urgent":false' "$(msg get_tasks)"
msg task 1 >/dev/null

# 8. overview: Mod+o, move with the arrows, Return goes there
key super+o
OV=$(xdotool search --name '^dwm-overview$' | head -1)
check "overview shown" IsViewable "$(shown "$OV")"
import -window root /out/overview.png 2>/dev/null || true
key super+2
check "Mod+2 does nothing while it is open" 0 "$(curdesk)"
check "still open" IsViewable "$(shown "$OV")"
key Down Right Right
key Return
check "overview closed" IsUnMapped "$(shown "$OV")"
check "went to task 2, tag 3" 12 "$(curdesk)"
key super+o
key Escape
check "Escape closes it" IsUnMapped "$(shown "$OV")"
check "and stays" 12 "$(curdesk)"
key super+o
key Up
key 5
check "a digit goes to that tag of the selected task" 4 "$(curdesk)"
msg overview >/dev/null; sleep 0.3
check "overview through IPC" IsViewable "$(shown "$OV")"
key super+o
check "Mod+o closes it" IsUnMapped "$(shown "$OV")"
# a click on a cell: the row of task 2, the last cell (tag 10)
key super+o
OX=$(xwininfo -id "$OV" | awk '/Absolute upper-left X/ {print $4}')
OY=$(xwininfo -id "$OV" | awk '/Absolute upper-left Y/ {print $4}')
OW=$(xwininfo -id "$OV" | awk '/Width:/ {print $2}')
echo "overview at $OX,$OY width $OW" >>"$OUT"
xdotool mousemove $((OX + OW - 8 - 6)) $((OY + 8 + 18 + 4 + 9)) click 1
sleep 0.4
check "click goes to task 2 tag 10" 19 "$(curdesk)"
check "click closed it" IsUnMapped "$(shown "$OV")"

echo "failures: $FAILS" >>"$OUT"
cat /tmp/dwm.log >>"$OUT"
cat "$OUT"
[ "$FAILS" -eq 0 ]
