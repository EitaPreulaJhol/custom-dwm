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

# dwm spawns these two by name (config.h), so expose them on PATH.
ln -sf "$dest/dwm-restart" "$bindir/dwm-restart"
ln -sf "$dest/dwm-exit"    "$bindir/dwm-exit"
ln -sf "$dest/dwm-clock"   "$bindir/dwm-clock"
ln -sf "$dest/dwm-session" "$bindir/dwm-session"

sed "s|@SESSION@|$dest/dwm-session|g" "$here/dwm.desktop.in" \
	> "$HOME/.local/share/xsessions/dwm.desktop"

cat <<EOF
Installed:
  $dest/dwm-session
  $dest/autostart.sh
  $dest/dwm-clock
  $dest/dwm-restart
  $dest/dwm-exit
  $HOME/.local/share/xsessions/dwm.desktop

Next steps:
  1. Install the WM binary (as root):  make clean install
  2. Install the focus outline:        (cd ../i3-focus-outline-rs && make install)
  3. Log in with the "dwm" session, or put this in ~/.xinitrc:
         exec "\$HOME/.dwm/dwm-session"

The autostart script expects dwm, i3-focus-outline and dwm-clock to be on PATH.
For a system-wide xsessions entry (display managers that ignore ~/.local):
  sudo install -m644 "$HOME/.local/share/xsessions/dwm.desktop" /usr/share/xsessions/dwm.desktop
EOF
