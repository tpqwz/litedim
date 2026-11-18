# litedim

Version 0.1.0-beta.2. Dim everything behind the focused window on XFCE, the way KDE's Dim Inactive effect and the Windows program LeDimmer do. A second shade can darken or tint the focused window itself.

The window manager stays xfwm4. The X server stays XLibre. The dim is one X11 client: a full-screen sheet stacked under the focused frame, a tray icon, and a settings window. All of that is the same executable.

`litedim` is a C program. `make` links the X libraries into one static executable, so the file you copy does not need a matching `libX11.so` on the other machine. It is built for the architecture you compile it on. It talks to the X server already running in `DISPLAY`. This checkout can live in a directory named `litedimmer`. The program and the public repository are `litedim`.

```
make
./litedim
./litedim --alpha 128 --color 000000
./litedim --fg-alpha 64 --fg-color 000000
./litedim --prev-alpha 48 --prev-color 000000
./litedim --no-tray
./litedim --version
```

The background coverage defaults to 96, out of 255. Black (`000000`) only darkens. Another color tints that shade. `--alpha 0` starts with both shades unmapped and remembers the levels. The foreground coverage defaults to 0, so the focused window stays bright until `--fg-alpha` or the Foreground slider raises it. That shade is the size of the focused frame and sits just above it, so panels stay bright. The settings window is left bright while it is focused. Last window is folded shut until you check it. Checked, its level and color open, and that window can be brighter or darker than the background. Unchecked, it is just part of the background shade. Child windows is off until you check it. Checked, a window and the dialogs that belong to it share one shade: the family you are in uses the foreground level, and the family you just left uses the last-window level.

One screen uses those Background sliders. With two or more screens, an All screens checkbox sits above them. Checked, that one level and color is copied onto every screen. Unchecked, each screen opens its own level and color, starting from the shared values. Foreground and Last window stay one setting for the whole desk. Unplugging a screen removes its block.

Either click on the tray icon opens a menu: Settings, Shade, Child windows, Last window, and Quit. Settings opens the window. Shade, Child windows, and Last window are checkboxes. Quit leaves. `--no-tray` leaves the shades running with no icon and no settings window. If the panel has no tray, the settings window opens on its own.

Pushes to GitHub build one Linux x86-64 executable named `litedim`. A tag such as `v0.1.0-beta.2` publishes that same file as a release. A tag that contains `-beta` is marked as a pre-release. Copy the file and run it. The machine you run it on needs an X server and a compositor. It does not need a matching `libX11`.

`litedim` is one file. Copy that file and run it. It does not need Python or GTK.

The same settings are a short line on a private socket, `/tmp/litedim-<uid>/control`, mode 0700:

```
alpha 128
color 000000
fg-alpha 64
fg-color 000000
prev-alpha 48
prev-color 000000
show
hide
status
quit
```

`alpha` and `color` are the background. `fg-alpha` and `fg-color` are the focused window. `prev-alpha` and `prev-color` are the window that was focused just before it. `alpha 0` removes the background shade and leaves the foreground shade in place. `hide` unmaps both and keeps the levels. The levels, colors, All screens choice, per-screen levels, and the settings-window position are stored in `~/.config/litedim/config`, so they survive a reboot. The first time that file is missing, an older `/tmp/litedim-<uid>/config` is read once. The settings window opens in the center of the screen until you move it. A second `./litedim` exits if the socket answers. A dead socket is replaced.

`make check` runs [cite](https://github.com/tpqwz/cite) on `src` for the MISRA C and CERT C rows that tool can see, Frama-C Eva on the arithmetic in `src/decide.c`, and a unit test. A quiet cite run means those rows were quiet. It is not a proof of the whole standards.

See [docs/ideas.md](docs/ideas.md) for the design, and [docs/research.md](docs/research.md) for the sources.
