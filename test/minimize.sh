#!/bin/sh
# Minimize from the taskbar, like Windows: a click on the focused window's
# entry minimizes it, a click on a minimized entry restores it exactly as it
# was, a click on another entry focuses it. Also _NET_WM_STATE_HIDDEN,
# WM_CHANGE_STATE, _NET_ACTIVE_WINDOW, floating/tabbed windows and restart.
# Exits non-zero on failure.
set -u

make -s clean all
cc -O2 -o /tmp/setbg test/setbg.c -lX11 || exit 1
cc -O2 -o /tmp/clientmsg test/clientmsg.c -lX11 || exit 1

export HOME=/tmp/minhome
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
OUT=/out/minimize.txt
: >"$OUT"
FAILS=0
pass() { echo "PASS $*" >>"$OUT"; }
fail() { echo "FAIL $*" >>"$OUT"; FAILS=$((FAILS + 1)); }
check() { # name expected actual
	if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: expected '$2', got '$3'"; fi
}
key() { xdotool key --clearmodifiers "$@"; sleep 0.3; }
ROOT=$(xwininfo -root | awk '/Window id:/ {print $4}')
shown() { xwininfo -id "$1" | awk '/Map State:/ {print $3}'; }
geom() { # absolute client geometry
	xwininfo -id "$1" | awk '/Absolute upper-left X/ {x=$4} /Absolute upper-left Y/ {y=$4}
		/Width:/ {w=$2} /Height:/ {h=$2} END {print x "," y " " w "x" h}'
}
active() { xdotool getactivewindow 2>/dev/null; }
wmstate() { xprop -id "$1" WM_STATE 2>/dev/null | awk '/window state:/ {print $3}'; }
hidden() { xprop -id "$1" -notype _NET_WM_STATE | grep -q _NET_WM_STATE_HIDDEN && echo yes || echo no; }
open() { # title [geometry] -> window id
	xmessage -name "$1" -title "$1" -geometry "${2:-300x200+100+100}" "$1" >/dev/null 2>&1 &
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
# x of the middle of the focused taskbar entry: the last run of orange
# (#FF8C00) pixels on the bar's top row; tag and task cells come first.
focusedentry() {
	import -window root -crop 1366x1+0+0 txt:- 2>/dev/null \
	| awk -F'[,:]' '/DarkOrange/ {x = $1 + 0; if (x != last + 1) start = x; last = x}
		END {print int((start + last) / 2)}'
}
clickbar() { xdotool mousemove --sync "$1" 9 click 1; sleep 0.4; xdotool mousemove 683 400; }
entryof() { # window -> taskbar x, by focusing it first
	/tmp/clientmsg "$1" _NET_ACTIVE_WINDOW 2 0 0; sleep 0.3
	focusedentry
}
activate() { /tmp/clientmsg "$1" _NET_ACTIVE_WINDOW 2 0 0; sleep 0.4; }

A=$(open one)
B=$(open two)
C=$(open three)
XA=$(entryof "$A") XB=$(entryof "$B") XC=$(entryof "$C")
echo "entries one=$XA two=$XB three=$XC" >>"$OUT"
activate "$B"
GA=$(geom "$A") GB=$(geom "$B") GC=$(geom "$C")
echo "before: $GA | $GB | $GC" >>"$OUT"

# 1. a click on the focused window's entry minimizes it
clickbar "$XB"
check "two hidden" IsUnviewable "$(shown "$B")"
check "two iconic (WM_STATE)" Iconic "$(wmstate "$B")"
check "two _NET_WM_STATE_HIDDEN" yes "$(hidden "$B")"
check "focus left two" yes "$([ "$(active)" != "$B" ] && echo yes || echo no)"
check "the others take its space" yes "$([ "$(geom "$A") $(geom "$C")" != "$GA $GC" ] && echo yes || echo no)"

# 2. a click on the minimized entry restores it exactly as it was
clickbar "$XB"
check "two shown again" IsViewable "$(shown "$B")"
check "two normal (WM_STATE)" Normal "$(wmstate "$B")"
check "two not hidden" no "$(hidden "$B")"
check "two focused" "$B" "$(active)"
check "layout as before" "$GA | $GB | $GC" "$(geom "$A") | $(geom "$B") | $(geom "$C")"

# 3. a click on another visible entry only focuses it
clickbar "$XA"
check "click on one focuses it" "$A" "$(active)"
check "one still shown" IsViewable "$(shown "$A")"
check "two still shown" IsViewable "$(shown "$B")"

# 4. a floating window keeps its position and size
activate "$C"
key super+space
key super+m
key Right Right Right Down Down
key Escape
GF=$(geom "$C")
clickbar "$(focusedentry)"
check "floating three hidden" IsUnviewable "$(shown "$C")"
clickbar "$XC"
check "floating three back in place" "$GF" "$(geom "$C")"
check "floating three focused" "$C" "$(active)"
key super+space

# 5. tabbed: the minimized tab leaves the strip and comes back selected
key super+w
activate "$A"
GT=$(geom "$A")
clickbar "$(focusedentry)"
check "tab one hidden" IsUnviewable "$(shown "$A")"
check "another tab shown" IsViewable "$(shown "$(active)")"
clickbar "$XA"
check "tab one back and selected" "$A IsViewable $GT" "$(active) $(shown "$A") $(geom "$A")"
key super+e

# 6. clients and pagers: WM_CHANGE_STATE iconifies, _NET_ACTIVE_WINDOW and
# _NET_WM_STATE remove restore
/tmp/clientmsg "$B" WM_CHANGE_STATE 3; sleep 0.4
check "WM_CHANGE_STATE iconic minimizes" IsUnviewable "$(shown "$B")"
activate "$B"
check "_NET_ACTIVE_WINDOW restores" "IsViewable $B" "$(shown "$B") $(active)"
/tmp/clientmsg "$B" _NET_WM_STATE 1 _NET_WM_STATE_HIDDEN; sleep 0.4
check "_NET_WM_STATE add hidden minimizes" IsUnviewable "$(shown "$B")"
/tmp/clientmsg "$B" _NET_WM_STATE 0 _NET_WM_STATE_HIDDEN; sleep 0.4
check "_NET_WM_STATE remove hidden restores" IsViewable "$(shown "$B")"

# 7. a minimized window stays minimized on another tag and after a restart
activate "$B"
clickbar "$(focusedentry)"
key super+2
key super+1
check "still minimized after a tag switch" IsUnviewable "$(shown "$B")"
kill -HUP "$DWM_PID"
sleep 1.2
check "still minimized after restart" "IsUnviewable Iconic" "$(shown "$B") $(wmstate "$B")"
activate "$B"
check "restored after restart" IsViewable "$(shown "$B")"

# 8. quitting hands a minimized window back mapped
activate "$B"
clickbar "$(focusedentry)"
kill -TERM "$DWM_PID"
sleep 0.8
check "visible on the root after quitting" IsViewable "$(shown "$B")"

cat /tmp/dwm.log >>"$OUT"
echo "failures: $FAILS" >>"$OUT"
cat "$OUT"
[ "$FAILS" -eq 0 ]
