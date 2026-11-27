#!/bin/sh
# Run dwm in a throw-away Xvfb and capture the bar. Intended for Docker:
#   docker run --rm -v "$PWD":/src -v "$OUT":/out dwm-test:local /src/test/run.sh
set -eu

# Nerd Font icons (optional; mounted from the host by the caller).
if [ -d /fonts ]; then
	mkdir -p /root/.local/share/fonts
	cp /fonts/* /root/.local/share/fonts/ 2>/dev/null || true
	fc-cache -f >/dev/null 2>&1 || true
fi

make -s clean all

DISPLAY_NUM="${DISPLAY_NUM:-99}"
export DISPLAY=":${DISPLAY_NUM}"

rm -f "/tmp/.X${DISPLAY_NUM}-lock" "/tmp/.X11-unix/X${DISPLAY_NUM}"

Xvfb ":${DISPLAY_NUM}" -screen 0 1366x768x24 -dpi 92 -nolisten tcp >/tmp/xvfb.log 2>&1 &
XVFB_PID=$!
trap 'kill $XVFB_PID 2>/dev/null || true' EXIT INT TERM

i=0
while [ ! -S "/tmp/.X11-unix/X${DISPLAY_NUM}" ] && [ $i -lt 200 ]; do
	sleep 0.025
	i=$((i + 1))
done
[ -S "/tmp/.X11-unix/X${DISPLAY_NUM}" ] || { cat /tmp/xvfb.log; exit 1; }

# Xvfb ignores root background changes; draw the wallpaper as a test window.
cc -O2 -o /tmp/setbg test/setbg.c -lX11
/tmp/setbg '#888888' &
SETBG_PID=$!
[ -n "${XFT_CONFIG:-}" ] && printf '%b\n' "$XFT_CONFIG" | xrdb -merge
xset s off 2>/dev/null || true
xset -dpms 2>/dev/null || true

./dwm >/tmp/dwm.log 2>&1 &
DWM_PID=$!
sleep 0.4

# Date/clock, normally fed by dwm-clock.
xsetroot -name "${STATUS_TEXT:-$(date '+%d-%m-%Y %H:%M')}"
sleep 0.2

if [ "${SHOW_TRAY:-0}" = 1 ]; then
	cc -O2 -o /tmp/trayicon test/trayicon.c -lX11
	/tmp/trayicon '#163561' &
	TRAY1=$!
	/tmp/trayicon '#175DDC' &
	TRAY2=$!
	/tmp/trayicon '#8800AA' &
	TRAY3=$!
	sleep 0.5
else
	TRAY1= TRAY2= TRAY3=
fi

mkdir -p /out
if [ "${SHOW_ST:-0}" = 1 ]; then
	for i in 1 2 3 4; do
		st -e sleep 30 >/dev/null 2>&1 &
	done
	sleep 0.8
fi

if [ "${SHOW_CLIENTS:-0}" = 1 ]; then
	xmessage -name 'st-256color' -title 'notes.txt - vim' -geometry 400x300+20+60 \
		'first window' >/dev/null 2>&1 &
	xmessage -name 'firefox' -title 'Mozilla Firefox' -geometry 500x300+500+60 \
		'second window' >/dev/null 2>&1 &
	xmessage -name 'pcmanfm' -title 'home' -geometry 300x200+700+300 \
		'third window' >/dev/null 2>&1 &
	sleep 0.8
fi

if [ "${TEST_FLOAT:-0}" = 1 ]; then
	xdotool key --clearmodifiers super+space
	sleep 0.3
	A=$(xdotool getactivewindow)
	X=$(xwininfo -id "$A" | awk '/Absolute upper-left X/ {print $4}')
	Y=$(xwininfo -id "$A" | awk '/Absolute upper-left Y/ {print $4}')
	W=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	H=$(xwininfo -id "$A" | awk '/Height:/ {print $2}')
	echo "before $X $Y ${W}x${H}" >/out/float.txt
	# client content is at (X,Y); the title bar is the 20px above it
	xdotool mousemove $((X + 100)) $((Y - 10)) mousedown 1
	sleep 0.1
	xdotool mousemove_relative -- 120 60
	sleep 0.1
	xdotool mouseup 1
	sleep 0.3
	A=$(xdotool getactivewindow)
	X=$(xwininfo -id "$A" | awk '/Absolute upper-left X/ {print $4}')
	Y=$(xwininfo -id "$A" | awk '/Absolute upper-left Y/ {print $4}')
	W=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	H=$(xwininfo -id "$A" | awk '/Height:/ {print $2}')
	echo "after $X $Y ${W}x${H}" >>/out/float.txt
	import -window root /out/float.png
fi

if [ "${TEST_TILEDRAG:-0}" = 1 ]; then
	A=$(xdotool getactivewindow)
	X=$(xwininfo -id "$A" | awk '/Absolute upper-left X/ {print $4}')
	Y=$(xwininfo -id "$A" | awk '/Absolute upper-left Y/ {print $4}')
	W=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	H=$(xwininfo -id "$A" | awk '/Height:/ {print $2}')
	echo "before $X $Y ${W}x${H}" >/out/tiledrag.txt
	xdotool keydown super
	xdotool mousemove $((X + 100)) $((Y - 10)) mousedown 1
	sleep 0.1
	xdotool mousemove_relative -- 120 60
	sleep 0.1
	xdotool mouseup 1
	xdotool keyup super
	sleep 0.4
	A=$(xdotool getactivewindow)
	X=$(xwininfo -id "$A" | awk '/Absolute upper-left X/ {print $4}')
	Y=$(xwininfo -id "$A" | awk '/Absolute upper-left Y/ {print $4}')
	W=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	H=$(xwininfo -id "$A" | awk '/Height:/ {print $2}')
	echo "after $X $Y ${W}x${H}" >>/out/tiledrag.txt
	import -window root /out/tiledrag.png
fi

if [ "${TEST_TAGSWITCH:-0}" = 1 ]; then
	xdotool key --clearmodifiers super+2
	sleep 0.4
	import -window root /out/tag2.png
	xdotool key --clearmodifiers super+1
	sleep 0.4
	import -window root /out/tagback.png
fi

if [ "${TAG_TEN:-0}" = 1 ]; then
	xdotool key --clearmodifiers super+shift+0
	sleep 0.4
fi
if [ "${TEST_TABS:-0}" = 1 ]; then
	cc -O2 -o /tmp/parent test/parent.c -lX11
	xdotool key --clearmodifiers super+w
	sleep 0.4
	import -window root /out/tabs-before.png
	A=$(xdotool getactivewindow)
	FP=$(/tmp/parent "$A")
	# the container's title strip (one cell per tab)
	TITLE=$(xdotool search --name '^dwm-tabs$' | head -1)
	TX=$(xwininfo -id "$TITLE" | awk '/Absolute upper-left X/ {print $4}')
	TY=$(xwininfo -id "$TITLE" | awk '/Absolute upper-left Y/ {print $4}')
	TW=$(xwininfo -id "$TITLE" | awk '/Width:/ {print $2}')
	TH=$(xwininfo -id "$TITLE" | awk '/Height:/ {print $2}')
	echo "frame=$FP title=$TITLE at $TX,$TY ${TW}x${TH}" >/out/tabs.txt
	echo "before active=$A focus=$(xdotool getwindowfocus)" >>/out/tabs.txt
	xdotool key --clearmodifiers super+Right
	sleep 0.4
	A=$(xdotool getactivewindow)
	echo "right1 active=$A focus=$(xdotool getwindowfocus)" >>/out/tabs.txt
	import -window root /out/tabs-right1.png
	xdotool key --clearmodifiers super+Right
	sleep 0.4
	A=$(xdotool getactivewindow)
	echo "right2 active=$A focus=$(xdotool getwindowfocus)" >>/out/tabs.txt
	import -window root /out/tabs-right2.png
	xdotool key --clearmodifiers super+Left
	sleep 0.4
	A=$(xdotool getactivewindow)
	echo "left1 active=$A focus=$(xdotool getwindowfocus)" >>/out/tabs.txt
	# three equal tabs; the middle one is the center of the title bar
	A=$(xdotool getactivewindow)
	xdotool mousemove --sync $((TX + TW / 2)) $((TY + TH / 2)) click 1
	sleep 0.5
	A=$(xdotool getactivewindow)
	echo "after active=$A focus=$(xdotool getwindowfocus)" >>/out/tabs.txt
	import -window root /out/tabs-middle.png
	xwininfo -root -tree >/out/tabs-tree.txt 2>&1
	# leave tabbed mode (layout toggle split): all frames must map again
	xdotool key --clearmodifiers super+e
	sleep 0.4
	xwininfo -id "$TITLE" | awk '/Map State/ {print "strip " $3}' >/out/tabs-exit-prop.txt 2>&1
	import -window root /out/tabs-exit.png
fi

if [ -n "${TEST_CORNER:-}" ]; then
	cc -O2 -o /tmp/parent test/parent.c -lX11
	A=$(xdotool getactivewindow)
	FP=$(/tmp/parent "$A")
	FX=$(xwininfo -id "$FP" | awk '/Absolute upper-left X/ {print $4}')
	FY=$(xwininfo -id "$FP" | awk '/Absolute upper-left Y/ {print $4}')
	FW=$(xwininfo -id "$FP" | awk '/Width:/ {print $2}')
	FH=$(xwininfo -id "$FP" | awk '/Height:/ {print $2}')
	FB=$(xwininfo -id "$FP" | awk '/Border width/ {print $3}')
	OW=$((FW + 2 * FB))
	OH=$((FH + 2 * FB))
	CW=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	CH=$(xwininfo -id "$A" | awk '/Height:/ {print $2}')
	echo "before frame $FX,$FY ${OW}x${OH} client ${CW}x${CH}" >/out/corner-$TEST_CORNER.txt
	case "$TEST_CORNER" in
		TL) BX=$((FX + 2)); BY=$((FY + 2)); DX=100; DY=100 ;;
		TR) BX=$((FX + OW - 3)); BY=$((FY + 2)); DX=-100; DY=100 ;;
		BL) BX=$((FX + 2)); BY=$((FY + OH - 3)); DX=100; DY=-100 ;;
		BR) BX=$((FX + OW - 3)); BY=$((FY + OH - 3)); DX=-100; DY=-100 ;;
		*) BX=$((FX + OW / 2)); BY=$((FY + OH / 2)); DX=100; DY=100 ;;
	esac
	xdotool mousemove --sync $BX $BY mousedown 3
	sleep 0.15
	xdotool mousemove_relative -- $DX $DY
	sleep 0.15
	xdotool mouseup 3
	sleep 0.4
	CW=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	CH=$(xwininfo -id "$A" | awk '/Height:/ {print $2}')
	echo "after client ${CW}x${CH}" >>/out/corner-$TEST_CORNER.txt
	import -window root /out/corner-$TEST_CORNER.png
fi

if [ "${TEST_BORDERRESIZE:-0}" = 1 ]; then
	cc -O2 -o /tmp/parent test/parent.c -lX11
	A=$(xdotool getactivewindow)
	FP=$(/tmp/parent "$A")
	FX=$(xwininfo -id "$FP" | awk '/Absolute upper-left X/ {print $4}')
	FY=$(xwininfo -id "$FP" | awk '/Absolute upper-left Y/ {print $4}')
	FW=$(xwininfo -id "$FP" | awk '/Width:/ {print $2}')
	FH=$(xwininfo -id "$FP" | awk '/Height:/ {print $2}')
	FB=$(xwininfo -id "$FP" | awk '/Border width/ {print $3}')
	CW=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	CH=$(xwininfo -id "$A" | awk '/Height:/ {print $2}')
	echo "before frame $FX,$FY ${FW}x${FH} bw=$FB client ${CW}x${CH}" >/out/borderresize.txt
	BX=$((FX + FW + FB + 2))
	BY=$((FY + FH / 2))
	xdotool mousemove --sync $BX $BY mousedown 3
	sleep 0.15
	xdotool mousemove_relative -- -100 0
	sleep 0.15
	xdotool mouseup 3
	sleep 0.4
	CW=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	CH=$(xwininfo -id "$A" | awk '/Height:/ {print $2}')
	echo "after client ${CW}x${CH}" >>/out/borderresize.txt
	import -window root /out/borderresize.png
fi

if [ "${TEST_RESIZE:-0}" = 1 ]; then
	xdotool key --clearmodifiers super+space
	sleep 0.3
	A=$(xdotool getactivewindow)
	X=$(xwininfo -id "$A" | awk '/Absolute upper-left X/ {print $4}')
	Y=$(xwininfo -id "$A" | awk '/Absolute upper-left Y/ {print $4}')
	W=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	H=$(xwininfo -id "$A" | awk '/Height:/ {print $2}')
	echo "before $X $Y ${W}x${H}" >/out/resize.txt
	xdotool keydown super
	xdotool mousemove $((X + 300)) $((Y + 300)) mousedown 3
	sleep 0.1
	xdotool mousemove_relative -- -100 -100
	sleep 0.1
	xdotool mouseup 3
	xdotool keyup super
	sleep 0.4
	A=$(xdotool getactivewindow)
	X=$(xwininfo -id "$A" | awk '/Absolute upper-left X/ {print $4}')
	Y=$(xwininfo -id "$A" | awk '/Absolute upper-left Y/ {print $4}')
	W=$(xwininfo -id "$A" | awk '/Width:/ {print $2}')
	H=$(xwininfo -id "$A" | awk '/Height:/ {print $2}')
	echo "after $X $Y ${W}x${H}" >>/out/resize.txt
	import -window root /out/resize.png
fi

if [ "${TEST_FULLSCREEN:-0}" = 1 ]; then
	xdotool key --clearmodifiers super+shift+f
	sleep 0.5
fi

if [ "${TEST_OUTLINE:-0}" = 1 ]; then
	cc -O2 -o /tmp/parent test/parent.c -lX11
	cc -O2 -o /tmp/inputshape test/inputshape.c -lX11 -lXext
	OUT=$(xwininfo -root -tree | awk '/"dwm-focus-outline"/ {print $1; exit}')
	A=$(xdotool getactivewindow)
	FP=$(/tmp/parent "$A")
	FX=$(xwininfo -id "$FP" | awk '/Absolute upper-left X/ {print $4}')
	FY=$(xwininfo -id "$FP" | awk '/Absolute upper-left Y/ {print $4}')
	FW=$(xwininfo -id "$FP" | awk '/Width:/ {print $2}')
	FH=$(xwininfo -id "$FP" | awk '/Height:/ {print $2}')
	FB=$(xwininfo -id "$FP" | awk '/Border width/ {print $3}')
	OW=$((FW + 2 * FB))
	OH=$((FH + 2 * FB))
	OX=$(xwininfo -id "$OUT" | awk '/Absolute upper-left X/ {print $4}')
	OY=$(xwininfo -id "$OUT" | awk '/Absolute upper-left Y/ {print $4}')
	OWO=$(xwininfo -id "$OUT" | awk '/Width:/ {print $2}')
	OHO=$(xwininfo -id "$OUT" | awk '/Height:/ {print $2}')
	{
		echo "frame $FP at $FX,$FY ${OW}x${OH} (border $FB)"
		echo "outline $OUT at $OX,$OY ${OWO}x${OHO}"
		if [ "$OUT" != "" ] && [ "$OX" = $((FX - 2)) ] \
		&& [ "$OY" = $((FY - 2)) ] && [ "$OWO" = $((OW + 4)) ] \
		&& [ "$OHO" = $((OH + 4)) ]; then
			echo "geometry ok"
		else
			echo "geometry mismatch"
		fi
		echo "plain shape:"
	} >/out/outline.txt
	/tmp/inputshape "$OUT" >>/out/outline.txt
	import -window root /out/outline.png

	# Fullscreen must hide the outline, leaving it must restore it.
	xdotool key --clearmodifiers super+shift+f
	sleep 0.4
	echo "fullscreen map=$(xwininfo -id "$OUT" | awk '/Map State/ {print $3}')" \
		>>/out/outline.txt
	import -window root /out/outline-fullscreen.png
	xdotool key --clearmodifiers super+shift+f
	sleep 0.4
	echo "restored map=$(xwininfo -id "$OUT" | awk '/Map State/ {print $3}')" \
		>>/out/outline.txt

	# Tabbed mode: the outline takes in the title strip, notched at the tab.
	xdotool key --clearmodifiers super+w
	sleep 0.4
	echo "tabbed shape:" >>/out/outline.txt
	/tmp/inputshape "$OUT" >>/out/outline.txt
	import -window root /out/outline-tabbed.png
fi

LAUNCHER_PID=
if [ "${TEST_LAUNCHER:-0}" = 1 ]; then
	printf 'firefox\nterminal\nfiles\neditor\nsettings\n' | \
		launcher-rs --dmenu --no-icons >/tmp/launcher.log 2>&1 &
	LAUNCHER_PID=$!
	sleep 1
	xdotool type --delay 80 'fi'
	sleep 0.4
fi

xwininfo -root -tree >/out/tree.txt 2>&1
cat /tmp/dwm.log >&2
import -window root /out/shot.png
import -window root -crop 1366x20+0+0 +repage /out/bar.png
xwd -root -silent >/out/root.xwd 2>/dev/null || true

kill "${SETBG_PID:-0}" 2>/dev/null || true
kill "${TRAY1:-0}" "${TRAY2:-0}" "${TRAY3:-0}" 2>/dev/null || true
if [ -n "${LAUNCHER_PID:-}" ]; then
	kill "$LAUNCHER_PID" 2>/dev/null || true
fi
kill "$DWM_PID" 2>/dev/null || true
wait "$DWM_PID" 2>/dev/null || true
[ -f /tmp/launcher.log ] && cat /tmp/launcher.log >&2 || true
