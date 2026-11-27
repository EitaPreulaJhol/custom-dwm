# dwm - i3 light look and controls

dwm 6.8 with the look and key bindings of my default i3 session (mostly for backup).

## Features

- **Frames and title bars** - 18px title bar with a real 16px
  `_NET_WM_ICON`, or the icon theme's PNG icon for the window class (no empty
  slot when there is none), i3-style border and colors. Drag the title to
  move, Win+Space toggles floating, Button3 resizes (on borders). Windows
  with client-side decorations (`_MOTIF_WM_HINTS`) get no title bar, except
  as tabs; ARGB windows get a 32-bit frame, so transparency survives.
- **Tabbed mode** - the title bar becomes a shared, clickable tab strip;
  `Mod+Left/Right` cycles tabs. Only the active tab is mapped, and remaps
  repaint the client so old content cannot linger.
- **Bar and taskbar** - 10 tags with the orange active frame, occupied dot,
  clock, 16px tray; taskbar items show real icons, titles, and an orange frame
  on the focused window.
- **Minimize** - the taskbar works like Windows': a click on the focused
  window's entry minimizes it (its entry is dimmed, the layout goes on
  without it), a click on a minimized entry restores it exactly as it was
  (same place in the order, floating geometry, tab, fullscreen) with the
  focus, a click on any other entry focuses it. Minimized windows are
  `Iconic` with `_NET_WM_STATE_HIDDEN`; a client's own minimize button
  (`WM_CHANGE_STATE`) and `_NET_ACTIVE_WINDOW` work too, and the state
  survives restarts.
- **Tasks** - groups of workspaces shown left of the tags. Each task has its
  own tags 1–10 with their own windows, layouts and factors, and remembers
  the viewed tags and the focused window of every monitor; the active task is
  global. `Mod+n` creates a task (its label is typed right in the bar),
  `Mod+Shift+n` (or `Button3` on the task) renames, `Mod+Shift+Delete`
  removes the active one and hands its windows to the neighbour. The labels
  are kept in `~/.dwm/tasks` (one per line); `deftasks` in `config.h` is only
  the initial list.
- **Layouts and gaps** - splith, splitv, monocle, tabbed, and floating, with
  uniform 10px inner and outer gaps. Layouts, the master size and the number
  of master windows are remembered per tag, so switching modes on one
  workspace does not change the others (when several tags are viewed
  together, the first one provides them). Each tiled window has its own share
  of its column/row, changed in resize mode like i3's `resize grow height`.
- **Floating windows** - any window that does not tile (floating rules,
  dialog/utility/splash/toolbar/notification types, transients, fixed-size
  prompts, the floating layout) always opens
  centered on its monitor, whatever position the client asked for. Clients
  adopted on a WM restart keep their position.
- **Prompts** - fixed-size GCR/keyring dialogs are floated and centered at
  their hinted size so their buttons are not clipped.
- **Focus outline** - a 2px orange outline around the focused frame, drawn by
  dwm on a shaped, input-transparent overlay; in tabbed mode it follows the
  focused tab with the i3 notch. Nothing external to install. The thickness is
  `outlinepx` in `config.h`; the color is `col_accent`.
- **Focus** - click to focus, no focus-follows-mouse. Switching tags, tabs
  or closing a window hands the keyboard to the newly focused client.
  `_NET_ACTIVE_WINDOW` follows i3's `focus_on_window_activation smart`: a
  window on a visible tag is focused, any other one is marked urgent (as is
  one that sets `_NET_WM_STATE_DEMANDS_ATTENTION`).
- **EWMH** - every tag of every task is an EWMH desktop (`task * 10 + tag`,
  named `task:tag`; `_NET_NUMBER_OF_DESKTOPS`,
  `_NET_DESKTOP_NAMES`, `_NET_CURRENT_DESKTOP`, `_NET_WM_DESKTOP`, all also
  settable by pagers), plus `_NET_CLOSE_WINDOW`, `_NET_WM_MOVERESIZE` (CSD
  apps dragging their own title bar) and `_NET_FRAME_EXTENTS`.
- **Restart** - `Mod+Shift+r` (or `dwm-restart`, i.e. SIGHUP) re-execs dwm in
  place and keeps every window's tags, monitor, floating state and size, the
  window order, the tasks, each tag's layout and factors, the viewed tags and
the focus.
  The state is kept in window properties all the time, so even a crashed dwm
  is restored. SIGTERM quits cleanly.
- **Session** - `integration/` has autostart, the clock, in-place restart, the
  exit confirmation, and the xsession selector.

Known gaps: no nested i3 tree (splith/splitv are separate layouts); the icon
theme lookup reads PNG icons only (SVG-only icons are not shown).

## Build and install

```sh
make
```

Dependencies: `libx11-dev`, `libxext-dev`, `libxft-dev`,
`libxinerama-dev`, `libfontconfig1-dev`, `libpng-dev` (icon theme lookup;
comment `ICONTHEME*` in `config.mk` to build without it).

```sh
make install
```

`make install` is the per-user install used by this setup. It installs `dwm`
and the man page under `~/.local` (`PREFIX` defaults to `~/.local`), then
refreshes the session files in `~/.dwm`, the helper symlinks and the xsession
entry via `integration/install.sh`. The binary is replaced atomically, so the
current dwm and X session are not interrupted; the new binary is used on the
next in-place restart (`Mod+Shift+r`) or login. On restart, `dwm-restart` also
stops any external `i3-focus-outline` helper left over from an older install,
since the outline is now part of dwm.

| Target | Purpose |
|---|---|
| `make install` | per-user binary, man page and session files |
| `make install-wm` | per-user binary and man page only |
| `make install-session` | per-user session files only |
| `make install-local` | alias for `make install-wm` |
| `make uninstall` | remove the installed binary and man page |

For a system-wide binary:

```sh
sudo make install PREFIX=/usr/local
```

That skips the per-user session files; run `./integration/install.sh` as
yourself if they still need installing. To stage a package:

```sh
make install-wm DESTDIR=/tmp/pkg PREFIX=/usr
```

`BINDIR` and `MANPREFIX` can be overridden separately; `DESTDIR` is prepended
to both.

With a display manager, choose the `dwm` session. Without one, `startx` uses
the installed `~/.xsession`:

```sh
startx                 # dwm
SESSION=i3 startx      # the old i3 session
```

Do not add `~/.xinitrc`: it bypasses `/etc/X11/Xsession` (dbus, `xrdb`,
ssh-agent). `Mod+Shift+r` makes dwm re-exec itself, so clients and helpers
stay alive and the newly installed binary takes over. The session wrapper
only starts dwm again if it exits or crashes (with a growing delay when it
keeps crashing) and logs its errors to `~/.dwm/dwm.log`. `dwm-restart` and
`dwm-exit` signal the dwm of the current display only (`dwm-wmpid`).

## Key bindings

`MODKEY = Mod4` (Super).

| Binding | Action |
|---|---|
| `Mod+Return` | start `st` |
| `Mod+f` / `Mod+d` | pcmanfm / launcher |
| `Mod+Shift+s` | flameshot |
| `Mod+q` | close the focused window |
| `Mod+arrows` | directional focus; Left/Right cycles tabs in tabbed/stacking |
| `Mod+Shift+arrows` | move the window (tiled: reorder; floating: 10px) |
| `Mod+h` / `Mod+v` | splith / splitv |
| `Mod+s` / `Mod+w` / `Mod+e` | stacking / tabbed / toggle split |
| `Mod+space` / `Mod+Shift+space` | toggle floating / focus the other |
| `Mod+Shift+f` | fullscreen |
| `Mod+1..0` / `Mod+Shift+1..0` | view / move to tags 1–10 (the current tag again: back and forth) |
| `Mod+Tab` | previous tag |
| `Mod+Ctrl+1..0` / `Mod+Alt+Shift+1..0` | switch to / move the window to task 1–10 (the active task again: back and forth) |
| `Mod+Page_Up` / `Mod+Page_Down` / ``Mod+` `` | previous / next / last task |
| `Mod+n` / `Mod+Shift+n` / `Mod+Shift+Delete` | new / rename / remove task (label typed in the bar: `Return` keeps, `Escape` cancels) |
| `Mod+r` / `Mod+m` | resize / move mode |
| `Mod+Shift+c` / `Mod+Shift+r` | restart dwm in place (state kept) |
| `Mod+Shift+e` | exit with confirmation |
| `Mod+b` / `Mod+j` / `Mod+k` | bar / focus stack |
| click a tab | focus that client |
| title `Button1` | focus; drag a floating window |
| `Mod`+title `Button1` | drag a tiled window onto another to swap them, or move a floating one |
| title `Button2` | toggle floating |
| title `Button3` | resize |
| border `Button3` / `Mod`+border `Button1/3` | resize / move or resize |

In resize mode use the arrows or `j/k/l/;` (shrink/grow width and height, as
in i3: on a tiled window one axis moves the master/stack split, the other one
changes the window's share of its column or row; `Shift+Up/Down` change the
number of master windows); in move mode use the arrows (Shift = 50px for
floating). `Return`, `Escape`, `Mod+r` or `Mod+m` leaves.
The mode keys are grabbed, so the focused client does not swallow them.

Bar clicks: tasks switch (`Button3` renames, `Mod`+click moves the window
there), tags switch, taskbar entries focus the window (the focused one is
minimized, a minimized one restored), the clock area opens the polybar calendar popup.

## Tests

The Docker harness runs dwm in Xvfb; it never touches the live session.

```sh
docker build -t dwm-test:local -f test/Dockerfile test/

docker run --rm -e SHOW_TRAY=1 -v "$PWD":/src -v /tmp/dwm-out:/out \
    -w /src dwm-test:local sh /src/test/run.sh
```

`test/run.sh` accepts `SHOW_CLIENTS`, `SHOW_ST`, `SHOW_TRAY` and the `TEST_*`
flags. `TEST_OUTLINE=1` (with `SHOW_CLIENTS=1`) checks the in-process focus
outline: geometry around the focused frame, empty input shape, hiding in
fullscreen, and the notched tabbed shape. `test/session.sh` covers
restart/exit; `test/focus.sh` checks that the keyboard really reaches the
focused client (tag switches, tabs, closing, short-lived windows, clicks,
fullscreen-at-map, `_NET_ACTIVE_WINDOW`) and exits non-zero on failure;
`test/icons.sh`, `test/modes.sh`, `test/fixed.sh`, and
`test/taglayouts.sh` cover icons, binding modes, fixed-size prompts, and
per-tag layouts. `test/restart.sh` checks that a restart (key and SIGHUP)
keeps the state and that SIGTERM hands the windows back; `test/tasks.sh` covers tasks (bar prompt, switching, per-task tags and
layouts, moving windows, EWMH desktops, restart, removal and
`~/.dwm/tasks` at login); `test/minimize.sh` covers minimizing from the
taskbar (tiled, floating and tabbed windows, client and pager requests,
restart); `test/ewmh.sh`
covers the EWMH/Motif support, ARGB frames, drag-to-swap, back-and-forth,
40 tabs and the icon theme fallback.

## Files

| Path | Purpose |
|---|---|
| `config.h` | the deployed configuration |
| `patches/` | upstream patches applied verbatim (`systray`) |
| `integration/` | session wrapper, autostart, clock, restart/exit |
| `test/` | Docker/Xvfb harness and helpers |
