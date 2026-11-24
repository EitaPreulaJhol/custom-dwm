# dwm - i3 light look and controls

dwm 6.8 with the look and key bindings of my default i3 session.

## Features

- **Frames and title bars** — 20px title bar with a real 16px
  `_NET_WM_ICON` (blank if absent), i3's 5px border and colors. Drag the title
  to move, Button2 toggles floating, Button3 resizes; plain right-drag on the
  border resizes from that corner or edge.
- **Tabbed mode** — the title bar becomes a shared, clickable tab strip;
  `Mod+Left/Right` cycles tabs. Only the active tab is mapped, and remaps
  repaint the client so old content cannot linger.
- **Bar and taskbar** — 10 tags with the orange active frame, occupied dot,
  clock, 16px tray; taskbar items show real icons, titles, and an orange frame
  on the focused window.
- **Layouts and gaps** — splith, splitv, monocle, tabbed, and floating, with
  uniform 10px inner and outer gaps.
- **Prompts** — fixed-size GCR/keyring dialogs are floated and centered at
  their hinted size so their buttons are not clipped.
- **Focus outline** — the `i3-focus-outline` helper was ported to EWMH/X11;
  it follows the real X focus and keeps the i3 tab notch (`_DWM_TAB`).
- **Session** — `integration/` has autostart, the clock, in-place restart, the
  exit confirmation, and the xsession selector.

Known gaps: no nested i3 tree (splith/splitv are separate layouts); a process
restart assigns hidden clients to the current tag; icons come only from
`_NET_WM_ICON` and there is no theme lookup (missing icons leave a blank slot).

## Build and install

```sh
make
```

Dependencies: `libx11-dev`, `libxft-dev`, `libxinerama-dev`,
`libfontconfig1-dev`.

```sh
sudo make clean install          # /usr/local/bin/dwm
# or
make install-local               # ~/.local/bin/dwm

(cd ../../Projects/i3-focus-outline-rs && make install)
./integration/install.sh
```

With a display manager, choose the `dwm` session. Without one, `startx` uses
the installed `~/.xsession`:

```sh
startx                 # dwm
SESSION=i3 startx      # the old i3 session
```

Do not add `~/.xinitrc`: it bypasses `/etc/X11/Xsession` (dbus, `xrdb`,
ssh-agent). The session wrapper re-execs dwm on exit, so `Mod+Shift+r` keeps
clients and helpers alive.

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
| `Mod+1..0` / `Mod+Shift+1..0` | view / move to tags 1–10 |
| `Mod+Tab` | previous tag |
| `Mod+r` / `Mod+m` | resize / move mode |
| `Mod+Shift+c` / `Mod+Shift+r` | restart dwm in place (hidden tags reset) |
| `Mod+Shift+e` | exit with confirmation |
| `Mod+b` / `Mod+j` / `Mod+k` | bar / focus stack |
| click a tab | focus that client |
| title `Button1` | focus; drag a floating window |
| `Mod`+title `Button1` | drag a tiled window (floats it) or move a floating one |
| title `Button2` | toggle floating |
| title `Button3` | resize |
| border `Button3` / `Mod`+border `Button1/3` | resize / move or resize |

In resize mode use the arrows or `j/k/l/;`; in move mode use the arrows
(Shift = 50px for floating). `Return`, `Escape`, `Mod+r` or `Mod+m` leaves.
The mode keys are grabbed, so the focused client does not swallow them.

Bar clicks: tags switch, `Mod`+tag moves the window, taskbar entries focus the
window, the clock area opens the polybar calendar popup.

## Tests

The Docker harness runs dwm in Xvfb; it never touches the live session.

```sh
docker build -t dwm-test:local -f test/Dockerfile test/

docker run --rm -e SHOW_TRAY=1 -v "$PWD":/src -v /tmp/dwm-out:/out \
    -w /src dwm-test:local sh /src/test/run.sh
```

`test/run.sh` accepts `SHOW_CLIENTS`, `SHOW_ST`, `SHOW_TRAY` and the `TEST_*`
flags. `test/session.sh` covers restart/exit; `test/icons.sh`,
`test/modes.sh`, and `test/fixed.sh` cover icons, binding modes, and fixed-size
prompts.

## Files

| Path | Purpose |
|---|---|
| `config.h` | the deployed configuration |
| `patches/` | upstream patches applied verbatim (`systray`) |
| `integration/` | session wrapper, autostart, clock, restart/exit |
| `test/` | Docker/Xvfb harness and helpers |
