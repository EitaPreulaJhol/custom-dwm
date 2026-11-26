#!/bin/sh
# Install the per-user dwm session files.
# Run from the dwm source tree: ./integration/install.sh
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
dest="$HOME/.dwm"
bindir="$HOME/.local/bin"

mkdir -p "$dest" "$bindir" "$HOME/.local/share/xsessions"

install -m755 "$here/dwm-session"   "$dest/dwm-session"
install -m755 "$here/autostart.sh"  "$dest/autostart.sh"
install -m755 "$here/dwm-clock"     "$dest/dwm-clock"
install -m755 "$here/dwm-restart"   "$dest/dwm-restart"
install -m755 "$here/dwm-exit"      "$dest/dwm-exit"
install -m755 "$here/dwm-wmpid"     "$dest/dwm-wmpid"
install -m755 "$here/xsession"      "$dest/xsession"

# dwm spawns these two by name (config.h), so expose them on PATH.
ln -sf "$dest/dwm-restart" "$bindir/dwm-restart"
ln -sf "$dest/dwm-exit"    "$bindir/dwm-exit"
ln -sf "$dest/dwm-wmpid"   "$bindir/dwm-wmpid"
ln -sf "$dest/dwm-clock"   "$bindir/dwm-clock"
ln -sf "$dest/dwm-session" "$bindir/dwm-session"

sed "s|@SESSION@|$dest/dwm-session|g" "$here/dwm.desktop.in" \
	> "$HOME/.local/share/xsessions/dwm.desktop"

# Without a display manager, startx uses ~/.xsession (via /etc/X11/Xsession).
# Never overwrite an existing session file.
if [ -e "$HOME/.xsession" ]; then
	echo "~/.xsession exists; left untouched (template: $dest/xsession)"
else
	install -m755 "$dest/xsession" "$HOME/.xsession"
	echo "Installed ~/.xsession (startx -> dwm, SESSION=i3 startx -> i3)"
fi

cat <<EOF
Installed:
  $dest/dwm-session
  $dest/autostart.sh
  $dest/dwm-clock
  $dest/dwm-restart
  $dest/dwm-exit
  $dest/dwm-wmpid
  $dest/xsession
  $HOME/.local/share/xsessions/dwm.desktop

Without a display manager, startx reads ~/.xsession:
  startx                 # dwm
  SESSION=i3 startx      # i3

Next steps:
  1. The WM binary is installed by 'make install' (or, system-wide,
     'sudo make install PREFIX=/usr/local').
  2. Log in with the "dwm" session, or (no DM) just run:
         startx                 # dwm
         SESSION=i3 startx      # i3

The autostart script expects dwm and dwm-clock to be on PATH.
For a system-wide xsessions entry (display managers that ignore ~/.local):
  sudo install -m644 "$HOME/.local/share/xsessions/dwm.desktop" /usr/share/xsessions/dwm.desktop
EOF
