#!/bin/sh
# EWMH/Motif support and the newer interactions: frame extents, windows with
# client-side decorations, ARGB frames, floating window types, desktops,
# close/move/resize requests, demands-attention, no workspace back-and-forth,
# drag-to-swap, many windows in a tabbed layout and icon theme fallback.
# Exits non-zero on failure.
set -u

make -s clean all
cc -O2 -o /tmp/setbg test/setbg.c -lX11 || exit 1
cc -O2 -o /tmp/ewmhwin test/ewmhwin.c -lX11 || exit 1
cc -O2 -o /tmp/clientmsg test/clientmsg.c -lX11 || exit 1

# A fake icon theme entry for the class "ThemeTest" (no _NET_WM_ICON).
export HOME=/tmp/ewmhhome
rm -rf "$HOME"
mkdir -p "$HOME/.local/share/icons/hicolor/32x32/apps"
convert -size 32x32 'xc:#ff0000' "$HOME/.local/share/icons/hicolor/32x32/apps/themetest.png"
# ... and an SVG-only one for "SvgTest" (blue square), rendered with nanosvg
mkdir -p "$HOME/.local/share/icons/hicolor/scalable/apps"
cat >"$HOME/.local/share/icons/hicolor/scalable/apps/svgtest.svg" <<'SVG'
<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 48 48">
  <rect x="0" y="0" width="48" height="48" rx="6" fill="#0000ff"/>
</svg>
SVG

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
trap 'pkill -x ewmhwin; kill $PIDS $DWM_PID $SETBG_PID $XVFB_PID 2>/dev/null || true' EXIT INT TERM
sleep 0.5

mkdir -p /out
OUT=/out/ewmh.txt
: >"$OUT"
FAILS=0
pass() { echo "PASS $*" >>"$OUT"; }
fail() { echo "FAIL $*" >>"$OUT"; FAILS=$((FAILS + 1)); }
check() { # name expected actual
	if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: expected '$2', got '$3'"; fi
}
key() { xdotool key --clearmodifiers "$@"; sleep 0.3; }
spawn() { # name class [opts] -> window id
	/tmp/ewmhwin "$@" >/dev/null 2>&1 &
	echo $! >/tmp/lastpid
	i=0
	while [ $i -lt 50 ]; do
		W=$(xdotool search --name "^$1\$" 2>/dev/null | head -1)
		[ -n "$W" ] && break
		sleep 0.05
		i=$((i + 1))
	done
	sleep 0.3
	echo "$W"
}
prop() { xprop -id "$1" -notype "$2" 2>/dev/null | sed -n 's/^[^=]*= *//p'; }
parent() { xwininfo -id "$1" -tree | awk '/Parent window id:/ {print $4}'; }
info() { xwininfo -id "$1" | awk -v k="$2" 'index($0, k) {print $NF}'; }
absx() { info "$1" "Absolute upper-left X:"; }
absy() { info "$1" "Absolute upper-left Y:"; }
curdesk() { prop "$(xwininfo -root | awk '/Window id:/ {print $4}')" _NET_CURRENT_DESKTOP; }
ROOT=$(xwininfo -root | awk '/Window id:/ {print $4}')

# a mapped title strip of a stacked/tabbed container?
tabstrip() {
	for w in $(xdotool search --name '^dwm-tabs$' 2>/dev/null); do
		xwininfo -id "$w" | grep -q IsViewable && { echo yes; return; }
	done
	echo no
}

# 1. _NET_FRAME_EXTENTS of a normal window: the 18px title bar
A=$(spawn wa ClassA)
check "frame extents" "0, 0, 18, 0" "$(prop "$A" _NET_FRAME_EXTENTS)"

# 2. _MOTIF_WM_HINTS without decorations: no title bar; as a tab, the
#    container's title strip above it shows its title
M=$(spawn wm ClassM -motif)
check "motif: no title extents" "0, 0, 0, 0" "$(prop "$M" _NET_FRAME_EXTENTS)"
check "motif: client at frame top" 0 "$(info "$M" "Relative upper-left Y:")"
key super+w
check "motif in tabbed: still no title bar" "0, 0, 0, 0" "$(prop "$M" _NET_FRAME_EXTENTS)"
check "motif in tabbed: title strip shown" yes "$(tabstrip)"
key super+e
check "motif back in splith" "0, 0, 0, 0" "$(prop "$M" _NET_FRAME_EXTENTS)"
check "no title strip in splith" no "$(tabstrip)"

# 3. ARGB client: the frame has its 32-bit depth
R=$(spawn wr ClassR -argb)
check "argb frame depth" 32 "$(info "$(parent "$R")" "Depth:")"
check "argb title depth" 24 "$(xwininfo -tree -id "$(parent "$R")" | awk '/^ +0x/ && !/wr/ {print $1; exit}' | xargs -I{} xwininfo -id {} | awk '/Depth:/ {print $2}')"

# 4. utility windows float at their own size
U=$(spawn wu ClassU -type UTILITY)
UPID=$(cat /tmp/lastpid)
check "utility floats (own width)" 300 "$(info "$U" "Width:")"

# 5. desktops
check "desktop names" '"main:1", "main:2", "main:3", "main:4", "main:5", "main:6", "main:7", "main:8", "main:9", "main:10"' "$(prop "$ROOT" _NET_DESKTOP_NAMES)"
/tmp/clientmsg "$A" _NET_WM_DESKTOP 4; sleep 0.3
check "_NET_WM_DESKTOP request moves A" 4 "$(prop "$A" _NET_WM_DESKTOP)"
check "A hidden" IsUnviewable "$(info "$A" "Map State:")"
/tmp/clientmsg "$ROOT" _NET_CURRENT_DESKTOP 4; sleep 0.3
check "_NET_CURRENT_DESKTOP request" 4 "$(curdesk)"
check "A visible on 5" IsViewable "$(info "$A" "Map State:")"
/tmp/clientmsg "$ROOT" _NET_CURRENT_DESKTOP 4; sleep 0.3
check "pager request for the current desktop is not back-and-forth" 4 "$(curdesk)"
key super+1
check "back on 1" 0 "$(curdesk)"

# 6. no workspace_auto_back_and_forth: the current tag again stays there
key super+3
key super+3
check "Mod+3 twice stays on 3" 2 "$(curdesk)"
key super+1

# 7. demands attention on an unfocused window
/tmp/clientmsg "$M" _NET_WM_STATE 1 _NET_WM_STATE_DEMANDS_ATTENTION 0; sleep 0.3
case "$(prop "$M" _NET_WM_STATE)" in
*DEMANDS_ATTENTION*) pass "demands attention published" ;;
*) fail "demands attention: _NET_WM_STATE=$(prop "$M" _NET_WM_STATE)" ;;
esac
case "$(xprop -id "$M" WM_HINTS)" in
*"urgency hint"*|*Urgency*|*urgent*) pass "demands attention -> urgent" ;;
*) fail "demands attention not urgent: $(xprop -id "$M" WM_HINTS)" ;;
esac

# 8. _NET_CLOSE_WINDOW
C=$(spawn wc ClassC)
CPID=$(cat /tmp/lastpid)
/tmp/clientmsg "$C" _NET_CLOSE_WINDOW 0 1; sleep 0.5
check "_NET_CLOSE_WINDOW closes" no "$(kill -0 $CPID 2>/dev/null && echo yes || echo no)"

# 9. _NET_WM_MOVERESIZE on the floating utility window
UX=$(absx "$U"); UY=$(absy "$U"); UW=$(info "$U" "Width:")
xdotool mousemove $((UX + 150)) $((UY + 100)) mousedown 1; sleep 0.2
/tmp/clientmsg "$U" _NET_WM_MOVERESIZE $((UX + 150)) $((UY + 100)) 8 1 1; sleep 0.2
xdotool mousemove $((UX + 250)) $((UY + 150)); sleep 0.2
xdotool mouseup 1; sleep 0.3
check "moveresize move x" $((UX + 100)) "$(absx "$U")"
check "moveresize move y" $((UY + 50)) "$(absy "$U")"
UX=$(absx "$U"); UY=$(absy "$U")
xdotool mousemove $((UX + 150)) $((UY + 100)) mousedown 1; sleep 0.2
/tmp/clientmsg "$U" _NET_WM_MOVERESIZE $((UX + 150)) $((UY + 100)) 4 1 1; sleep 0.2
xdotool mousemove $((UX + 200)) $((UY + 140)); sleep 0.2
xdotool mouseup 1; sleep 0.3
check "moveresize size (bottom-right) grows" yes "$([ "$(info "$U" "Width:")" -gt "$UW" ] && echo yes || echo no)"
/tmp/clientmsg "$U" _NET_WM_MOVERESIZE 0 0 8 1 1; sleep 0.3
UX2=$(absx "$U")
xdotool mousemove 5 300; sleep 0.3
check "moveresize after release is ignored" "$UX" "$(absx "$U")"
kill "$UPID" 2>/dev/null; sleep 0.3

# 10. drag a tiled window onto another: they swap, both stay tiled
key super+6
P=$(spawn wp ClassP)
Q=$(spawn wq ClassQ)
PX=$(absx "$P"); QX=$(absx "$Q"); QW=$(info "$Q" "Width:")
xdotool mousemove $((PX + 100)) $(( $(absy "$P") + 100 ))
xdotool keydown super mousedown 1; sleep 0.2
xdotool mousemove $((QX + 100)) $(( $(absy "$Q") + 100 )); sleep 0.3
xdotool mouseup 1 keyup super; sleep 0.4
check "drag swap: P takes Q's place" "$QX" "$(absx "$P")"
check "drag swap: Q takes P's place" "$PX" "$(absx "$Q")"
check "drag swap: P still tiled (Q's cell width)" "$QW" "$(info "$P" "Width:")"

# 11. 40 windows in a tabbed layout (no 32-window limit)
key super+7
for n in $(seq 1 40); do
	/tmp/ewmhwin "many$n" Many >/dev/null 2>&1 &
	PIDS="$PIDS $!"
done
sleep 2
key super+w
FIRST=$(xdotool getactivewindow)
for n in $(seq 1 40); do xdotool key --clearmodifiers super+Right; done
sleep 0.5
check "dwm alive with 40 tabs" yes "$(kill -0 $DWM_PID 2>/dev/null && echo yes || echo no)"
check "40x Mod+Right cycles back" "$FIRST" "$(xdotool getactivewindow)"
check "title strip with 40 tabs" yes "$(tabstrip)"

# 12. icon from the icon theme (class ThemeTest -> themetest.png, red)
key super+8
T=$(spawn wt ThemeTest)
FR=$(parent "$T")
FX=$(absx "$FR"); FY=$(absy "$FR")
PIX=$(import -window root -crop 1x1+$((FX + 16))+$((FY + 9)) -depth 8 txt:- 2>/dev/null | sed -n 's/.*\(#[0-9A-Fa-f]\{6\}\).*/\1/p' | head -1)
check "theme icon drawn in the title" "#FF0000" "$PIX"

# 13. SVG icon from the icon theme (class SvgTest -> scalable svgtest.svg)
key super+9
SV=$(spawn ws SvgTest)
FR=$(parent "$SV")
FX=$(absx "$FR"); FY=$(absy "$FR")
PIX=$(import -window root -crop 1x1+$((FX + 16))+$((FY + 9)) -depth 8 txt:- 2>/dev/null | sed -n 's/.*\(#[0-9A-Fa-f]\{6\}\).*/\1/p' | head -1)
check "svg theme icon drawn in the title" "#0000FF" "$PIX"

import -window root /out/ewmh-end.png 2>/dev/null || true
cat /tmp/dwm.log >>"$OUT"
echo "failures: $FAILS" >>"$OUT"
cat "$OUT"
[ "$FAILS" -eq 0 ]
