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

if command -v i3-focus-outline >/dev/null 2>&1; then
	i3-focus-outline >/tmp/outline.log 2>&1 &
	OUTLINE_PID=$!
else
	OUTLINE_PID=
fi
sleep 0.2

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
if [ "${SHOW_CLIENTS:-0}" = 1 ]; then
	xmessage -name 'st-256color' -title 'notes.txt - vim' -geometry 400x300+20+60 \
		'first window' >/dev/null 2>&1 &
	xmessage -name 'firefox' -title 'Mozilla Firefox' -geometry 500x300+500+60 \
		'second window' >/dev/null 2>&1 &
	xmessage -name 'pcmanfm' -title 'home' -geometry 300x200+700+300 \
		'third window' >/dev/null 2>&1 &
	sleep 0.8
fi

if [ "${TAG_TEN:-0}" = 1 ]; then
	xdotool key --clearmodifiers super+shift+0
	sleep 0.4
fi
if [ "${TEST_FULLSCREEN:-0}" = 1 ]; then
	xdotool key --clearmodifiers super+shift+f
	sleep 0.5
fi

LAUNCHER_PID=
if [ "${TEST_LAUNCHER:-0}" = 1 ]; then
	printf 'firefox\nterminal\nfiles\neditor\nsettings\n' | \
		i3-launcher --dmenu --no-icons >/tmp/launcher.log 2>&1 &
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

[ -n "${OUTLINE_PID:-}" ] && kill "$OUTLINE_PID" 2>/dev/null || true
kill "${SETBG_PID:-0}" 2>/dev/null || true
kill "${TRAY1:-0}" "${TRAY2:-0}" "${TRAY3:-0}" 2>/dev/null || true
kill "${LAUNCHER_PID:-0}" 2>/dev/null || true
kill "$DWM_PID" 2>/dev/null || true
wait "$DWM_PID" 2>/dev/null || true
[ -f /tmp/outline.log ] && cat /tmp/outline.log >&2
[ -f /tmp/launcher.log ] && cat /tmp/launcher.log >&2
