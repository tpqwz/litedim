# litedim

Version 0.1.0-beta.1. Dim everything behind the focused window on XFCE, the way KDE's Dim Inactive effect and the Windows program LeDimmer do. A second shade can darken or tint the focused window itself.

The window manager stays xfwm4. The X server stays XLibre. The dim is one X11 client: a full-screen sheet stacked under the focused frame, a tray icon, and a settings window. All of that is the same executable.

`litedim` is a C program. `make` links the X libraries into one static executable, so the file you copy does not need a matching `libX11.so` on the other machine. It is built for the architecture you compile it on. It talks to the X server already running in `DISPLAY`. This checkout can live in a directory named `litedimmer`. The program and the public repository are `litedim`.

```
make
./litedim
./litedim --alpha 128 --color 000000
./litedim --fg-alpha 64 --fg-color 000000
./litedim --no-tray
./litedim --version
```

The background coverage defaults to 96, out of 255. Black (`000000`) only darkens. Another color tints that shade. `--alpha 0` starts with both shades unmapped and remembers the levels. The foreground coverage defaults to 0, so the focused window stays bright until `--fg-alpha` or the Foreground slider raises it. That shade is the size of the focused frame and sits just above it, so panels stay bright. The settings window is left bright while it is focused.

The tray icon opens the settings window: background level and color, foreground level and color, hide, and quit. Right-click the icon hides both shades. `--no-tray` leaves the shades running with no icon and no settings window. If the panel has no tray, the settings window opens on its own.

Pushes to GitHub build one Linux x86-64 executable named `litedim`. A tag such as `v0.1.0-beta.1` publishes that same file as a release. A tag that contains `-beta` is marked as a pre-release. Copy the file and run it. The machine you run it on needs an X server and a compositor. It does not need a matching `libX11`.

`litedim` is one file. Copy that file and run it. It does not need Python or GTK.

The same settings are a short line on a private socket, `/tmp/litedim-<uid>/control`, mode 0700:

```
alpha 128
color 000000
fg-alpha 64
fg-color 000000
show
hide
status
quit
```

`alpha` and `color` are the background. `fg-alpha` and `fg-color` are the focused window. `alpha 0` removes the background shade and leaves the foreground shade in place. `hide` unmaps both and keeps the levels. The same directory holds a `config` file that the next start reads. A second `./litedim` exits if that socket answers. A dead socket is replaced.

`make check` runs [cite](https://github.com/tpqwz/cite) on `src` for the MISRA C and CERT C rows that tool can see, Frama-C Eva on the arithmetic in `src/decide.c`, and a unit test. A quiet cite run means those rows were quiet. It is not a proof of the whole standards.

See [docs/ideas.md](docs/ideas.md) for the design, and [docs/research.md](docs/research.md) for the sources.
