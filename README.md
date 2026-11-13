# litedim

Dim everything behind the focused window on XFCE, the way KDE's Dim Inactive effect and the Windows program LeDimmer do.

The window manager stays xfwm4. The X server stays XLibre. The dim is one X11 client: a full-screen sheet stacked under the focused frame, a tray icon, and a settings window. All of that is the same executable.

`litedim` is a C program. `make` links the X libraries into one static executable, so the file you copy does not need a matching `libX11.so` on the other machine. It is built for the architecture you compile it on. It talks to the X server already running in `DISPLAY`. This checkout can live in a directory named `litedimmer`. The program and the public repository are `litedim`.

```
make
./litedim
./litedim --alpha 128 --color 000000
./litedim --no-tray
```

The default coverage is 96, out of 255. Black (`000000`) only darkens. Another color tints the shade. `--alpha 0` starts with the sheet unmapped and remembers the previous strength. The tray icon opens the settings window: level, red, green, blue, hide, and quit. Right-click the icon hides the shade. `--no-tray` leaves the sheet running with no icon and no settings window. If the panel has no tray, the settings window opens on its own.

`litedim` is one file. Copy that file and run it. It does not need Python or GTK.

The same settings are a short line on a private socket, `/tmp/litedim-<uid>/control`, mode 0700:

```
alpha 128
color 000000
show
hide
status
quit
```

`hide` unmaps the sheet and keeps the level. The same directory holds a `config` file that the next start reads. A second `./litedim` exits if that socket answers. A dead socket is replaced.

`make check` runs [cite](https://github.com/tpqwz/cite) on `src` for the MISRA C and CERT C rows that tool can see, Frama-C Eva on the arithmetic in `src/decide.c`, and a unit test. A quiet cite run means those rows were quiet. It is not a proof of the whole standards.

See [docs/ideas.md](docs/ideas.md) for the design, and [docs/research.md](docs/research.md) for the sources.
