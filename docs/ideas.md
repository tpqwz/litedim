# Dim the background, leave the focused window bright

litedimmer (litedim) is for this desk: XFCE, xfwm4, GTK apps, XLibre. The aim is the same one KDE ships as Dim Inactive, and the same one Lionel Lemarié's LeDimmer shipped for Windows: the window you are working in stays at full brightness, and the rest of the screen is darkened.

Two products are easy to mix up. LeDimmer is the Windows program (LeDimmer.exe, about 1.0.0.4, 2007). This repository is litedimmer.

## What "dim" means

Three different operations get called dimming. Only the first two match the aim.

**Darken.** Multiply the red, green, and blue of a pixel by a factor below 1. The pixel stays opaque. Text stays solid and becomes darker. KWin does this with `WindowPaintData::multiplyBrightness`. picom's GL shader does `c.rgb * (1.0 - dim)`. A translucent black sheet laid over already-drawn pixels does the same arithmetic: source-over of black at alpha A leaves `pixel * (1 - A)`.

**A sheet under the focused window.** One full-screen translucent black window sits immediately under the focused window. Everything stacked below that sheet is darkened, including the wallpaper, desktop icons, and other windows. The focused window is above the sheet, so it is unchanged. This is LeDimmer. The author's changelog describes it as a black transparent fullscreen window under the foreground window ([llemarie/ledimmer](https://sites.google.com/site/llemarie/ledimmer)). The author's page does not name the command-line flags or a default alpha. Reviews of the program describe a 0–255 alpha with a default of 150. Those numbers are not on the page that was opened, so litedim takes the alpha as a parameter and does not copy a default from the reviews.

**Opacity.** The window is blended with whatever is behind it. Wallpaper shows through the letters. xfwm4's inactive-window control is this kind. On this machine it is currently 100, so it is not doing anything, and turning it down would not look like KWin or LeDimmer.

KWin and LeDimmer also differ in what they darken:

| | Focused window | Other windows | Wallpaper and icons | Panel |
| --- | --- | --- | --- | --- |
| KWin Dim Inactive, default options | full brightness | darkened | left alone (`dimDesktop` defaults off) | left alone (`dimPanels` defaults off) |
| LeDimmer | full brightness | darkened | darkened | reviews say the taskbar stays bright; the sheet is under the foreground window, so anything stacked above that window stays bright |

KWin's strength defaults to 25 and is then clamped into 0.1–0.9. The fade is about 160 ms when a window brightens and about 250 ms when it dims. With the defaults, docks, the desktop, and keep-above windows stay bright, fullscreen windows are dimmed, and windows in the same group as the focused one stay bright (an app and its settings dialog). Popups and input-method windows are never dimmed. Source: [`diminactive.cpp`](https://invent.kde.org/plasma/kwin/-/raw/master/src/plugins/diminactive/diminactive.cpp) (`dimWindow`, `canDimWindow`) and [`diminactive.kcfg`](https://sources.debian.org/src/kwin-x11/4%3A6.6.5-2/src/plugins/diminactive/diminactive.kcfg/).

## This machine

Checked on this session:

- Session is XFCE on X11. `XDG_CURRENT_DESKTOP=XFCE`, `DISPLAY=:0`.
- Server vendor string is `XLibre`, release 12502002. Package `xserver-xlibre-core` is 25.2.2. One output, `DP-0` at 3440×1440.
- Window manager is Xfwm4 4.18.0. The same Xfwm4 window (`0x800336`) owns the compositing-manager selection `_NET_WM_CM_S0`. A second compositor cannot start while that selection is held.
- Compositor is on: `/general/use_compositing` is true. `/general/inactive_opacity` and `/general/frame_opacity` are 100. `/general/move_opacity` is 89.
- Extensions present: Composite, Render, Damage, Present, RandR, Shape, XFixes.
- Stacking, top of the list: three `xfce4-panel` windows of type `_NET_WM_WINDOW_TYPE_DOCK`, then normal application windows. The panels are above every normal window.
- picom is not installed.

## What xfwm4 can already do

xfwm4 4.18 applies inactive opacity in `clientSetOpacity` by multiplying the window's `_NET_WM_WINDOW_OPACITY` value by `inactive_opacity / 100` ([`client.c` in the 4.18.0 tag](https://github.com/xfce-mirror/xfwm4/blob/xfwm4-4.18.0/src/client.c)). The compositor then builds an alpha mask with `solid_picture(..., opacity, 0, 0, 0)` and composites with `PictOpOver` ([`compositor.c`](https://github.com/xfce-mirror/xfwm4/blob/xfwm4-4.18.0/src/compositor.c)). The mask changes coverage. It does not scale the window's color. `blackPicture` in that file is the shadow color, drawn under windows, and is a separate thing.

The settings GUI for this is Window Manager Tweaks, Compositor tab. The xfconf keys are `inactive_opacity`, `frame_opacity`, `popup_opacity`, `move_opacity`, `resize_opacity`.

That path is the wrong effect for litedim.

## Ways to get the effect

### 1. A sheet under the focused window

A small X client maps one override-redirect ARGB window the size of the screen, fills it with black at the chosen alpha, and stacks it immediately below the focused window's frame. Clicks pass through it. xfwm4's compositor, which is already running, blends the sheet. Nothing else has to be replaced.

This matches LeDimmer, and it darkens the wallpaper as well as the other windows. It is the approach to build.

It does not match KWin's default, where the desktop stays bright. That difference is the point of the sheet: the sheet darkens the already-composited image under it, so the desktop darkens too. KWin darkens each window's texture and, unless `dimDesktop` is on, paints the desktop at full brightness.

### 2. picom's inactive dim

picom has had `--inactive-dim` for years. Current picom prefers window rules, with a `dim` field from 0 to 1. On the `next` branch the paint path builds a tint of `(1 - dim)` per channel and multiplies the window body by it, in both the GL and xrender backends ([`command_builder.c`](https://github.com/yshui/picom/blob/next/src/renderer/command_builder.c), [`shaders.c`](https://github.com/yshui/picom/blob/next/src/backend/gl/shaders.c)). The shadow tint is separate, so shadows stay undimmed, and the root pixmap is copied at full brightness. The man page is [picom(1)](https://www.mankier.com/1/picom). The legacy option treats docks, the desktop, dialogs, and menus as focused, so it leaves them bright. A rule such as `match = "!focused"; dim = 0.25` does dim those windows, including the xfdesktop window, which is what the user sees as wallpaper and icons. Docks can be excluded so the panel stays bright.

picom has to be the compositor. It takes `_NET_WM_CM_S0`. xfwm4 holds that selection today, so picom means turning `/general/use_compositing` off and living with picom's shadows and fading instead of xfwm4's. Issue [xfce/xfwm4#756](https://gitlab.xfce.org/xfce/xfwm4/-/issues/756) records that the two were never meant to run together. This is a good way to preview the look. It is a poor fit for a desk that wants to keep xfwm4's compositor.

### 3. Teach xfwm4 to darken

The durable XFCE version of KWin's effect is a change in `paint_win`: when the window is inactive, scale its color, then composite. XRender can do that by drawing the window into a temporary picture and then source-overing black at the dim alpha, which is the same `pixel * (1 - A)` as the sheet, applied per window. Dialogs in the focused group, docks, and the desktop can be exempted the way `canDimWindow` does.

That is a fork or a patch against xfwm4 4.18, kept in step with XFCE releases. [xfwm4-gl](https://github.com/fulalas/xfwm4-gl) is an OpenGL fork of the same compositor, which would make a multiply cheaper, and it is still a window-manager fork. Worth it if the sheet's stacking turns out to fight xfwm4. A later step.

### 4. GTK

A normal GTK window is the wrong layer. GTK draws one application's pixels. It cannot rescale another application's window. A GTK window that tried to be the sheet would also want focus, a taskbar entry, and a frame unless those are all switched off.

A normal GTK window is the wrong layer for the sheet, and a second GTK program would not be the single executable. The settings window and the tray icon are X clients in the same binary as the sheet.

### 5. XLibre

XLibre on this machine is an X server with the usual extensions: Composite, Render, Shape, XFixes, Damage, RandR, Present. Dimming is a policy about which window is focused. The server does not know that policy. The compositing manager does, or a client that watches `_NET_ACTIVE_WINDOW` does. Patching the server would still need a client to decide the strength and the exceptions.

The Composite overlay window (`CompositeGetOverlayWindow`) is the compositor's own front buffer. xfwm4 owns it. A client drawing there would fight the compositor. The sheet is an ordinary composited window, which xfwm4 already knows how to blend.

## Design of the sheet

One program, `litedim`, in C (`src/litedim.c`, with the decisions in `src/decide.c`). It talks to the X server and does not replace xfwm4. `make` produces one statically linked executable. libX11, libXrender, libXfixes, and libXext are inside that file. GTK is not linked. The tray icon speaks the XEmbed system-tray protocol, and the settings window is an ordinary X client in the same process.

`make check` runs cite on `src` for the MISRA C and CERT C rows that parse can decide, and Frama-C Eva on `src/decide.c` for the coverage math, the stacking decision, and the control line. A quiet cite run means those rows were quiet. It is not a proof of either standard. The sheet does not call `exit`, `getenv`, the stdio functions, or anything in `<signal.h>`, because those rows are in the check.

**Window.** Override-redirect, so xfwm4 does not reparent it or give it a frame. Depth 32, ARGB visual, matching colormap. Mapped on the root window. Filled once with black at the chosen alpha, using an XRender picture. No taskbar entry, because override-redirect windows are not managed. One sheet the size of the root. LeDimmer 1.0.0.4 uses a single window for the screen. Stacking is one list for the whole screen, so a sheet under the focused frame already covers every monitor for the windows below that frame. Resize the sheet when the root size changes. A sheet per monitor, so one output can be left bright, can wait.

**Alpha.** Command-line `--alpha N`, a fraction of full black from 0 to 255. The author's page does not publish a default, so the program needs one of its own and should print it. 0 unmaps the sheet.

**Clicks.** The sheet must not take clicks, or clicking a dimmed window could not focus it. Set the input shape to an empty region with XFixes (`XFixesCreateRegion` with no rectangles, then `XFixesSetWindowShapeRegion` on `ShapeInput`). Passing `None` would restore the default input shape. The bounding shape stays the full rectangle, so the pixels still show. Pointer events fall through to the window underneath. This is the X11 equivalent of Win32 `WS_EX_TRANSPARENT`.

**Who is focused.** Read `_NET_ACTIVE_WINDOW` on the root window (EWMH). Select `PropertyChangeMask` on the root so a change wakes the program.

**Where the sheet sits.** `_NET_ACTIVE_WINDOW` is the client window. xfwm4 reparents that client into a frame, and the frame is the root's child. `XConfigureWindow` can only stack a window against a sibling. Walk `XQueryTree` from the client up until the parent is the root. That window is the sibling. Then `XConfigureWindow` the sheet with `CWSibling` and `CWStackMode` `Below`. The focused frame stays where it is, and the sheet moves to just underneath it.

On this desk the panels are the top three windows in `_NET_CLIENT_LIST_STACKING`, above every normal window. A sheet placed below the focused frame stays below the panels, so the panels stay bright. That matches the LeDimmer reviews in which the taskbar stays lit. If a panel is ever stacked below normal windows, look for `_NET_WM_WINDOW_TYPE_DOCK` and raise those windows above the sheet.

**When the desktop is focused.** LeDimmer 1.0.0.1 cleared the dim when the desktop was selected. 1.0.0.4 dimmed the desktop even then. Default for litedim is the 1.0.0.4 behavior. If `_NET_ACTIVE_WINDOW` is `None` or the window is `_NET_WM_WINDOW_TYPE_DESKTOP`, keep the sheet mapped and place it above the desktop window. Stacking it `Below` the desktop frame would leave the wallpaper bright. A flag can hide the sheet in that case.

**Menus and tooltips.** They are override-redirect and are created above other windows. The sheet moves when the active window changes. It should stay put while a menu is open, so the menu remains above the sheet. Listening only to `_NET_ACTIVE_WINDOW`, plus a restack when the focused frame itself is restacked, is enough. Restacking on every `SubstructureNotify` would fight menus.

**Dialogs.** A modal dialog becomes `_NET_ACTIVE_WINDOW`, so it stays bright and its parent dims. That is LeDimmer's behavior. KWin can keep the whole group bright. Group tracking can wait.

**Fullscreen.** xfwm4 puts fullscreen clients on a layer above docks. A focused fullscreen window covers the sheet where that window covers the screen. Unredirect is narrower than "any fullscreen window." In `map_win`, xfwm4 unredirects a native-opaque unshaped window when `bypass_compositor == 1`, or when `unredirect_overlays` is true and the window is override-redirect with `bypass_compositor == 0`. `unredirect_overlays` defaults to true. The first such window unmaps the compositor overlay, so the sheet leaves the screen entirely, not only on that window's rectangle. A normal fullscreen application that does not set `_NET_WM_BYPASS_COMPOSITOR` stays redirected, and the sheet stays under it.

**Shadows.** xfwm4 paints a window's shadow in the same compositor pass as the window. The focused window and its shadow are above the sheet. Other windows' shadows are below the sheet and darken with them.

**Cost.** The sheet's contents do not change. The compositor blends one extra fullscreen rectangle when something under it is damaged. At 3440×1440 that is cheap next to a shader pass over every window.

**Toggle.** Either click on the tray icon opens a menu: Settings, Shade, Child windows, Last window, and Quit. Shade hides the sheets and keeps the levels. There is no second program.

**Foreground.** A second override-redirect sheet, the size of the focused frame, stacked directly above that frame. It uses the same premultiplied tint as the background sheet, with its own level and color (`--fg-alpha`, `--fg-color`, and the Foreground sliders). The default level is 0, so the focused window stays bright. The sheet is not fullscreen, so a panel stacked above the frame stays bright, and a menu stacked above the frame stays bright. While the settings window is the active client, the foreground sheet is unmapped so the controls stay clear. This is shade coverage, not `_NET_WM_WINDOW_OPACITY`: the focused window does not become see-through.

**Last window.** A check box in the settings window. Unchecked, the section is collapsed and that frame stays under the background shade. Checked, the level and color sliders open. Level 0 leaves the window bright. A higher level is that window's own shade. It is one window, not a group: a dialog and its parent are not kept together, unless Child windows is on.

**Child windows.** A check box, off until you turn it on. On, windows joined by `WM_TRANSIENT_FOR` are one family: the parent and its dialogs. The focused family takes the foreground level on every member. The family that was focused just before takes the last-window level when that section is open, and the background level when it is not. Up to eight frames in a family. Menus and the panel stay as they are.

**Screens.** One output keeps a single background sheet the size of the root, and the Background sliders are that sheet. Two or more outputs each get a sheet the size of that output. The output under the focused window stacks its sheet beneath that frame. Each other output stacks its sheet beneath the panel, so a window there does not stay bright only because it sits above the focused frame in the one global stack. All screens is a checkbox, on unless you turn it off. On, the Background level and color are copied to every output. Off, the dialog grows one level and color block per output, and those values can diverge. Turning it back on copies the shared level and color over the per-screen values. Foreground and Last window are not per screen.

**What this version leaves out.** Animation of the strength. Per-window exceptions other than docks, the settings window, the optional child-window family, and that one previous family. A BMP image in place of flat black, which LeDimmer had as `-image`.

## Order of work

1. `src/litedim.c` maps the sheet, punches an empty input shape, and restacks it under the focused frame when `_NET_ACTIVE_WINDOW` changes. Alpha comes from `--alpha`. `make` writes the static binary. It has been run on this session with xfwm4's compositor left on.
2. Watch the real annoyances: menus landing under the sheet, the panel dimming, the sheet stealing focus, a fullscreen video, a window moving to another monitor. Fix those before adding a GUI.
3. The tray and the settings window are in the `litedim` binary. Background level and color, and foreground level and color, are separate. The socket accepts `alpha`, `color`, `fg-alpha`, `fg-color`, `show`, `hide`, `status`, and `quit`.
4. Only if the sheet cannot be made to stack reliably, patch `paint_win` in xfwm4, or run picom with `dim` rules and xfwm4 compositing off.

picom is available as a preview of per-window darkening. Turning xfwm4's compositor off changes this session's shadows and opacity, so that preview waits for an explicit go-ahead.

## Sources

The longer source notes, with the quotes and the file paths, are in [research.md](research.md).

- KWin Dim Inactive: [`diminactive.cpp`](https://invent.kde.org/plasma/kwin/-/raw/master/src/plugins/diminactive/diminactive.cpp). `dimWindow` calls `multiplyBrightness` and `multiplySaturation`. `canDimWindow` lists the exceptions. The 2018 rewrite notes are in [D13720](https://phabricator.kde.org/D13720).
- picom dim: [picom(1)](https://www.mankier.com/1/picom) (`--inactive-dim`, window-rule `dim`). Shader: `c.rgb * (1.0 - dim)` in [`shaders.c`](https://github.com/yshui/picom/blob/master/src/backend/gl/shaders.c).
- xfwm4 4.18.0: [`clientSetOpacity`](https://github.com/xfce-mirror/xfwm4/blob/xfwm4-4.18.0/src/client.c) multiplies by `inactive_opacity`. [`compositor.c`](https://github.com/xfce-mirror/xfwm4/blob/xfwm4-4.18.0/src/compositor.c) `solid_picture` plus `PictOpOver` applies that as an alpha mask.
- LeDimmer: [author's page](https://sites.google.com/site/llemarie/ledimmer). Changelog line for 1.0.0.0: a black transparent fullscreen window under the foreground window. 1.0.0.4: command-line parameters, dims the desktop even when it is focused, one window covering the screen.
- Shape input region: [X Shape protocol](https://www.x.org/releases/X11R7.7/doc/xextproto/shape.html). An empty client input region leaves no effective input region, so the pointer goes to windows below.
- One compositor: the owner of the selection `_NET_WM_CM_S0` on this display is the Xfwm4 window `0x800336`.
