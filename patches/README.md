# Patches

- `01-systray-6.8.diff` — upstream `dwm-systray-6.8.diff`, applied verbatim
  (only the icon sizing was adjusted afterwards in `dwm.c` so the tray matches
  the 16px polybar reference).

The other i3-look features are implemented directly in `dwm.c`/`config.h`:
gaps, the tag frames/dots, the taskbar, the layouts and the binding modes.
Keeping those as separately applicable diffs against dwm 6.8 was more fragile
than maintaining this small fork.
