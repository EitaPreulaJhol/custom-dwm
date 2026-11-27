#!/bin/sh
# Tasks: groups of workspaces, each with its own tags 1..10. Creating and
# renaming in the bar, switching (and back and forth), moving windows, per
# task layouts, EWMH desktops, restart, removal and the ~/.dwm/tasks file
# read by a fresh dwm (login). Exits non-zero on failure.
set -u

make -s clean all
cc -O2 -o /tmp/setbg test/setbg.c -lX11 || exit 1
cc -O2 -o /tmp/clientmsg test/clientmsg.c -lX11 || exit 1

export HOME=/tmp/taskshome LANG=C.UTF-8 LC_ALL=C.UTF-8
rm -rf "$HOME"
mkdir -p "$HOME"
TASKS="$HOME/.dwm/tasks"

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
OUT=/out/tasks.txt
: >"$OUT"
FAILS=0
pass() { echo "PASS $*" >>"$OUT"; }
fail() { echo "FAIL $*" >>"$OUT"; FAILS=$((FAILS + 1)); }
check() { # name expected actual
	if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: expected '$2', got '$3'"; fi
}
key() { xdotool key --clearmodifiers "$@"; sleep 0.3; }
ROOT=$(xwininfo -root | awk '/Window id:/ {print $4}')
rootprop() { xprop -root -notype "$1" | sed -n 's/.* = //p'; }
desk() { xprop -id "$1" -notype _NET_WM_DESKTOP 2>/dev/null | sed -n 's/.* = //p'; }
curdesk() { rootprop _NET_CURRENT_DESKTOP; }
ndesk() { rootprop _NET_NUMBER_OF_DESKTOPS; }
shown() { xwininfo -id "$1" | awk '/Map State:/ {print $3}'; }
width() { xwininfo -id "$1" | awk '/Width:/ {print $2}'; }
active() { xdotool getactivewindow 2>/dev/null; }
tasksfile() { tr '\n' ',' <"$TASKS" 2>/dev/null; }
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

# 1. defaults: one task from config.h, no file yet
check "one task: 10 desktops" 10 "$(ndesk)"
check "default task name" '"main:1"' "$(rootprop _NET_DESKTOP_NAMES | cut -d, -f1)"
check "no tasks file before an edit" no "$([ -e "$TASKS" ] && echo yes || echo no)"
W1=$(open one)
check "one on main:1" 0 "$(desk "$W1")"

# 2. Mod+n creates a task after the current one and asks for its label
key super+n
xdotool type --delay 30 "work ção"
sleep 0.2
key BackSpace BackSpace BackSpace BackSpace
key Return
check "tasks file after new + rename" "main,work," "$(tasksfile)"
check "two tasks: 20 desktops" 20 "$(ndesk)"
check "new task is active" 10 "$(curdesk)"
check "second desktop name" '"work:1"' "$(rootprop _NET_DESKTOP_NAMES | cut -d, -f11 | tr -d ' ')"
check "one hidden in the other task" IsUnviewable "$(shown "$W1")"
W2=$(open two)
check "two on work:1" 10 "$(desk "$W2")"

# 3. switching: Mod+Ctrl+N, again = back and forth, Mod+grave, Page_Up/Down
key ctrl+super+1
check "Mod+Ctrl+1 goes to main" 0 "$(curdesk)"
check "one shown in main" IsViewable "$(shown "$W1")"
check "two hidden in main" IsUnviewable "$(shown "$W2")"
check "focus back on one" "$W1" "$(active)"
key ctrl+super+1
check "Mod+Ctrl+1 again goes back" 10 "$(curdesk)"
check "focus back on two" "$W2" "$(active)"
key super+grave
check "Mod+grave previous task" 0 "$(curdesk)"
key super+Next
check "Mod+Page_Down next task" 10 "$(curdesk)"
key super+Next
check "Mod+Page_Down wraps" 0 "$(curdesk)"
key ctrl+super+5
check "missing task ignored" 0 "$(curdesk)"

# 4. tags are per task: main:3 and work:3 are different workspaces
key super+3
check "main:3" 2 "$(curdesk)"
key ctrl+super+2
check "work keeps its own tag 1" 10 "$(curdesk)"
key ctrl+super+1
check "main keeps tag 3" 2 "$(curdesk)"
key super+1

# 5. layouts are per task x tag: work:1 tabbed, main:1 still split
key ctrl+super+2
W3=$(open three)
key super+w
TABW=$(width "$W3")
key ctrl+super+1
W4=$(open four)
SPLITW=$(width "$W4")
echo "tabbed $TABW split $SPLITW" >>"$OUT"
check "work:1 tabbed is full width" yes "$([ "$TABW" -gt 1200 ] && echo yes || echo no)"
check "main:1 still split" yes "$([ "$SPLITW" -lt 800 ] && echo yes || echo no)"

# 6. Mod+Alt+Shift+N moves the focused window, keeping its tag
check "four focused" "$W4" "$(active)"
key super+alt+shift+2
check "four moved to work:1" 10 "$(desk "$W4")"
check "four hidden after the move" IsUnviewable "$(shown "$W4")"
check "still in main" 0 "$(curdesk)"

# 6b. Mod+F1..F12 move the focused window to task 1..12, keeping its tag
F=$(active); FD=$(desk "$F")
key super+F2
check "Mod+F2 moves to work" $((FD + 10)) "$(desk "$F")"
/tmp/clientmsg "$F" _NET_WM_DESKTOP "$FD"; sleep 0.3
check "moved back" "$FD" "$(desk "$F")"
key super+F3
check "Mod+F3 without a third task does nothing" "$FD" "$(desk "$F")"

# 7. pager requests address task x tag
/tmp/clientmsg "$ROOT" _NET_CURRENT_DESKTOP 13; sleep 0.3
check "_NET_CURRENT_DESKTOP 13 = work:4" 13 "$(curdesk)"
/tmp/clientmsg "$W1" _NET_WM_DESKTOP 12; sleep 0.3
check "_NET_WM_DESKTOP 12 moves one to work:3" 12 "$(desk "$W1")"
key super+1
/tmp/clientmsg "$W1" _NET_WM_DESKTOP 0; sleep 0.3
check "_NET_WM_DESKTOP 0 moves it back" 0 "$(desk "$W1")"

# 8. rename the active task with Mod+Shift+n; Escape keeps the old label
key super+shift+n
xdotool type --delay 30 "nope"
key Escape
check "Escape keeps the label" "main,work," "$(tasksfile)"
key super+shift+n
xdotool type --delay 30 "proj"
key Return
check "Mod+Shift+n renames" "main,proj," "$(tasksfile)"

# 9. restart keeps tasks, the active task, windows and focus
snapshot() { echo "$(desk "$W1") $(desk "$W2") $(desk "$W3") $(desk "$W4")|$(curdesk)|$(active)|$(ndesk)"; }
BEFORE=$(snapshot)
echo "before: $BEFORE" >>"$OUT"
kill -HUP "$DWM_PID"
sleep 1.2
AFTER=$(snapshot)
echo "after: $AFTER" >>"$OUT"
check "state after restart" "$BEFORE" "$AFTER"
check "focused window still shown" IsViewable "$(shown "$(active)")"
check "one still hidden" IsUnviewable "$(shown "$W1")"
key ctrl+super+1
check "main after restart" 0 "$(curdesk)"
check "one shown after restart" IsViewable "$(shown "$W1")"
key ctrl+super+2
check "work:1 still tabbed after restart" yes "$([ "$(width "$W3")" -gt 1200 ] && echo yes || echo no)"

# 10. a third task, then Mod+Shift+Delete hands its windows to the
# neighbour (here: proj), which becomes active
key super+n
key Return
check "third task with the lowest free number" "main,proj,1," "$(tasksfile)"
W5=$(open five)
check "five on 1:1" 20 "$(desk "$W5")"
key super+shift+Delete
check "task removed from the file" "main,proj," "$(tasksfile)"
check "neighbour is active" 10 "$(curdesk)"
check "five moved to proj:1" 10 "$(desk "$W5")"
check "20 desktops again" 20 "$(ndesk)"
key ctrl+super+1
key super+shift+Delete
check "first task removed" "proj," "$(tasksfile)"
check "everything on proj" "0 0 0 0 0" "$(desk "$W1") $(desk "$W2") $(desk "$W3") $(desk "$W4") $(desk "$W5")"
key super+shift+Delete
check "the last task is never removed" "proj," "$(tasksfile)"

# 11. login: a fresh dwm without _DWM_STATE reads the labels from the file
key super+n
xdotool type --delay 30 "mail"
key Return
kill -TERM "$DWM_PID"
sleep 0.8
xprop -root -remove _DWM_STATE
./dwm >>/tmp/dwm.log 2>&1 &
DWM_PID=$!
sleep 0.8
check "labels read at login" '"proj:1"|"mail:1"' \
	"$(rootprop _NET_DESKTOP_NAMES | cut -d, -f1 | tr -d ' ')|$(rootprop _NET_DESKTOP_NAMES | cut -d, -f11 | tr -d ' ')"
check "login starts on the first task" 0 "$(curdesk)"
import -window root -crop 1366x20+0+0 /out/tasks-bar.png 2>/dev/null || true

cat /tmp/dwm.log >>"$OUT"
echo "failures: $FAILS" >>"$OUT"
cat "$OUT"
[ "$FAILS" -eq 0 ]
