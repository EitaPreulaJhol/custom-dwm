#!/bin/sh
# Once-per-session startup, mirroring the reference i3 config.
set -u

[ -n "${DISPLAY:-}" ] || exit 1

# XDG autostart entries (i3 config: dex --environment i3).
if command -v dex >/dev/null 2>&1; then
	dex --autostart --environment dwm >/dev/null 2>&1 &
fi

# Network applet.
if command -v nm-applet >/dev/null 2>&1; then
	nm-applet >/dev/null 2>&1 &
fi

# Focus outline helper (EWMH/X11; survives dwm restarts).
if command -v i3-focus-outline >/dev/null 2>&1; then
	# comm is truncated to 15 chars, so -x must use the truncated name
	pkill -x i3-focus-outlin 2>/dev/null || true
	i3-focus-outline >/dev/null 2>&1 &
fi

# Polkit authentication agent.
if [ -x /usr/lib/policykit-1-gnome/polkit-gnome-authentication-agent-1 ]; then
	/usr/lib/policykit-1-gnome/polkit-gnome-authentication-agent-1 >/dev/null 2>&1 &
fi

# No screen blanking / DPMS (i3 config).
xset s off 2>/dev/null || true
xset -dpms 2>/dev/null || true
xset s noblank 2>/dev/null || true

# Root background and DPI (i3 config).
xsetroot -solid '#888888'
xrandr --dpi 92 2>/dev/null || true

# Clock in the dwm bar (writes the root WM_NAME, read by updatestatus()).
"$HOME/.dwm/dwm-clock" >/dev/null 2>&1 &
