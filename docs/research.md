# Dimming inactive windows on XFCE / xfwm4 / XLibre

This note is for **litedimmer** (the client name **litedim**). It is not Lionel Lemarié's Windows program LeDimmer, and it is not a product named LiteDimmer.

Two reference behaviors:

- **KWin Dim Inactive** darkens inactive windows by multiplying their pixels. The active window stays bright. Panels, the desktop, and keep-above windows stay bright unless options say otherwise.
- **LeDimmer** (Le Dimmer, `LeDimmer.exe`, Lionel Lemarié, v1.0.0.4) dims the screen apart from the active window, including the desktop. The author's words for the first version are a black transparent fullscreen window stacked under the foreground window.

Three different operations get called "dim". They are not interchangeable.

| Operation | What the pixels do | Looks like |
| --- | --- | --- |
| Brightness multiply | RGB of a window's texture is scaled by a factor below 1. Alpha stays put. | Text stays solid and gets darker. Nothing shows through the letters. |
| Black source-over sheet | A black pixel at alpha A is composited with Render `Over`: `C = Ca * 1 + Cb * (1 - Aa)`. Premultiplied black has `Ca` rgb of 0, so covered rgb becomes `Cb * (1 - A)`. An opaque destination stays opaque. | Same darken as a multiply, for every pixel the sheet covers. The windows under the sheet are not made see-through. |
| Window opacity | The window is blended with whatever is already behind it (`_NET_WM_WINDOW_OPACITY`, an alpha mask, `PictOpOver`). | Wallpaper and other windows show through the letters. |

KWin and picom's `dim` are the first. A LeDimmer-style sheet is the second. xfwm4's inactive/frame/popup/move/resize opacity settings are the third.

## What LeDimmer's author actually wrote

Opened: [llemarie/ledimmer](https://sites.google.com/site/llemarie/ledimmer).

- "Le Dimmer is a small Windows 2000/XP/Vista application that dims the screen apart from the active window."
- **1.0.0.0:** "Display a black transparent fullscreen window under the foreground window." Quit with Ctrl-shift-q.
- **1.0.0.1:** multi-monitor support, BMP support, "Undims screen when desktop is selected", tray icon.
- **1.0.0.3:** many 512×512 windows instead of one fullscreen window, for large displays and multi-monitor.
- **1.0.0.4:** "Added command line parameters and usage dialog." "By default dims the desktop even when focused and uses a single window to cover the screen."

No source was published on that page. The page does not name command-line flags, does not give an alpha range, and does not give a default alpha. It does not say the window has a hole, and it does not say clicks pass through. A program called Dimmer Pro was not on the page that was opened. The mechanism in the author's words is a black transparent fullscreen window **under** the foreground window. Version 1.0.0.4 keeps that dim up when the desktop itself is focused, and uses one window for the screen.

## a. KWin `DimInactiveEffect` — true multiply, per window

Behavior: **true darken** of the windows it selects. Not translucency. Not an overlay. The desktop and the panels are left at full brightness with the shipped defaults, so this is not LeDimmer.

Opened on KWin master:

- [`src/plugins/diminactive/diminactive.cpp`](https://invent.kde.org/plasma/kwin/-/raw/master/src/plugins/diminactive/diminactive.cpp)
- [`src/plugins/diminactive/diminactive.kcfg`](https://invent.kde.org/plasma/kwin/-/raw/master/src/plugins/diminactive/diminactive.kcfg)
- [`src/effect/effect.cpp`](https://invent.kde.org/plasma/kwin/-/raw/master/src/effect/effect.cpp)
- [`src/scene/workspacescene.cpp`](https://invent.kde.org/plasma/kwin/-/raw/master/src/scene/workspacescene.cpp)
- [`src/scene/itemrenderer_opengl.cpp`](https://invent.kde.org/plasma/kwin/-/raw/master/src/scene/itemrenderer_opengl.cpp)
- [`src/opengl/base.frag`](https://invent.kde.org/plasma/kwin/-/raw/master/src/opengl/base.frag)
- [`src/opengl/glshadermanager.cpp`](https://invent.kde.org/plasma/kwin/-/raw/master/src/opengl/glshadermanager.cpp)
- [`src/scene/windowitem.cpp`](https://invent.kde.org/plasma/kwin/-/raw/master/src/scene/windowitem.cpp)

`paintWindow` calls `dimWindow` when `canDimWindow` is true. `dimWindow` does:

```
data.multiplyBrightness(1.0 - strength * dimFactor);
data.multiplySaturation(1.0 - strength * dimFactor);
```

`WindowPaintData::multiplyBrightness` is `d->brightness *= factor`. The same for saturation. It does not call `multiplyOpacity`.

`WorkspaceScene::paintWindow` builds a `WindowPaintData` and passes it through effects. `finalDrawWindow` calls `renderItem` on `w->windowItem()` with that same `data`. `ItemRendererOpenGL::renderItem` walks the item and its children. For every render node, if `data.brightness() != 1` it sets `ShaderTrait::Modulate` and `ShaderTrait::TransformColorspace`. `modulate(opacity, brightness)` returns rgb = `opacity * brightness`. The shader, when `TRAIT_MODULATE` is set, does `result *= modulation`. The comment in `itemrenderer_opengl.cpp` says brightness and saturation adjustments are applied in linear space. `glshadermanager.cpp` turns `ShaderTrait::TransformColorspace` into `TRAIT_TRANSFORM_COLORSPACE`.

`WindowItem::updateShadowItem` parents a `ShadowItem` on the window item and `stackBefore`s the window container. `createRenderNode` paints children, and the brightness uniform is the window's `WindowPaintData`, not a per-child one. The shadow is multiplied with the window. Nothing in this path skips the shadow.

`dimFactor` is 0 while `effects->activeFullScreenEffect()` is set, and it animates through `m_fullScreenTransition` otherwise. Per-window transitions use `animationTime(160ms)` when a window brightens (`scheduleInTransition`) and `animationTime(250ms)` when it dims (`scheduleOutTransition`).

`reconfigure` sets:

```
m_dimStrength = std::clamp(DimInactiveConfig::strength() / 100.0, 0.1, 0.9);
```

kcfg group `Effect-diminactive`:

| Entry | Default |
| --- | --- |
| `Strength` | 25 |
| `DimPanels` | false |
| `DimDesktop` | false |
| `DimKeepAbove` | false |
| `DimByGroup` | true |
| `DimFullScreen` | true |

Default strength 25 is inside the clamp, so the multiply factor is `1 - 0.25 = 0.75` on brightness and on saturation.

`canDimWindow` returns false (window stays bright) when:

- the window is `m_activeWindow`
- `m_dimByGroup` and `belongToSameGroup` (`w1->group() == w2->group()`), the comment's example being an app window and its preferences window
- `isDock()` and not `m_dimPanels`
- `isDesktop()` and not `m_dimDesktop`
- `keepAbove()` and not `m_dimKeepAbove`
- `isFullScreen()` and not `m_dimFullScreen`
- `isPopupWindow()` or `isInputMethod()` — these are never dimmed, there is no option
- `isX11Client() && !isManaged()` — unmanaged X11 is never dimmed

It then returns true only for `isNormalWindow()`, `isDialog()`, `isUtility()`, `isDock()`, or `isDesktop()`. Other types are not dimmed.

So KWin, at defaults, darkens unfocused normal, dialog, and utility windows, including their shadows, and does not darken the active window, its group, docks, the desktop, keep-above windows, popups, input methods, or unmanaged X11 windows. Fullscreen windows are dimmed unless a fullscreen effect is running. The desktop wallpaper is not a window body in this effect unless `DimDesktop` is on.

## b. picom `--inactive-dim` and window-rule `dim` — multiply of the window body

Behavior: **true darken** of the window body (and its frame, when the frame is drawn as part of that body). Not window opacity. Not a sheet over the desktop. The shadow is not dimmed. The root pixmap is not dimmed. With the legacy option, docks, the desktop, dialogs, and menus are treated as focused, so they are not dimmed either.

Opened on `yshui/picom` branch `next`:

- [`man/picom.1.adoc`](https://raw.githubusercontent.com/yshui/picom/next/man/picom.1.adoc)
- [`src/options.c`](https://raw.githubusercontent.com/yshui/picom/next/src/options.c)
- [`src/config_libconfig.c`](https://raw.githubusercontent.com/yshui/picom/next/src/config_libconfig.c)
- [`src/wm/win.c`](https://raw.githubusercontent.com/yshui/picom/next/src/wm/win.c)
- [`src/wm/win.h`](https://raw.githubusercontent.com/yshui/picom/next/src/wm/win.h)
- [`src/wm/defs.h`](https://raw.githubusercontent.com/yshui/picom/next/src/wm/defs.h)
- [`src/c2.c`](https://raw.githubusercontent.com/yshui/picom/next/src/c2.c)
- [`src/renderer/command_builder.c`](https://raw.githubusercontent.com/yshui/picom/next/src/renderer/command_builder.c)
- [`src/backend/gl/shaders.c`](https://raw.githubusercontent.com/yshui/picom/next/src/backend/gl/shaders.c)
- [`src/backend/xrender/xrender.c`](https://raw.githubusercontent.com/yshui/picom/next/src/backend/xrender/xrender.c)
- [`include/picom/types.h`](https://raw.githubusercontent.com/yshui/picom/next/include/picom/types.h)
- [`src/picom.c`](https://raw.githubusercontent.com/yshui/picom/next/src/picom.c)

The man page:

- `--inactive-dim VALUE` — "Dim inactive windows. (0.0 - 1.0, defaults to 0.0). Using this option is discouraged."
- `--inactive-dim-fixed` — "Use fixed inactive dim value, instead of adjusting according to window opacity."
- Window-rule key `dim` — "Dim level of the matching window. Larger value means more dimming. (0.0 - 1.0)". `opacity` is a separate key.
- "If window rules option is used, none of the above options will have any effect" for the superseded list, which includes `inactive-dim`, `inactive-opacity`, `mark-wmwin-focused`, `mark-ovredir-focused`, and `focus-exclude`.
- Migration text: with rules "the compositor no longer have an 'active window' concept". The replacement match is `focused || group_focused`, plus `|| wmwin` for `--mark-wmwin-focused` and `|| override_redirect` for `--mark-ovredir-focused`.
- `--use-ewmh-active-win` — "Use EWMH `_NET_ACTIVE_WINDOW` to determine currently focused window, rather than listening to 'FocusIn'/'FocusOut' event."
- `--detect-transient` uses `WM_TRANSIENT_FOR` so the group is focused together. `--detect-client-leader` uses `WM_CLIENT_LEADER`. `WM_TRANSIENT_FOR` wins if both are on.
- Shader interface: "If a custom shader is used, the default picom effects (e.g. dimming, color inversion, etc.) will no longer be automatically applied." The shader must call `default_post_processing`. "The xrender backend doesn't support shader at all."

`set_default_winopts` says focused/unfocused state only applies to a few types; "all other windows are always considered focused." The types with `focus = false` are `WINTYPE_UNKNOWN`, `WINTYPE_NORMAL`, and `WINTYPE_UTILITY`. Every other `wintype_t` gets `focus = true`: desktop, dock, toolbar, menu, splash, dialog, dropdown_menu, popup_menu, tooltip, notification, combo, dnd.

`win_update_dim` runs only inside `win_on_factor_change` when `list_is_empty(&ps->o.rules)`. It sets `w->options.dim = ps->o.inactive_dim` when `inactive_dim > 0 && !focused`, else 0. `focused` there is `win_is_focused`, which is true when the window is viewable and (`is_focused` or `is_group_focused`), or when the wintype `focus` flag is set, or `--mark-wmwin-focused`, or `--mark-ovredir-focused`, or `--focus-exclude` matches. So legacy `--inactive-dim` does not dim the XFCE desktop, the panel, dialogs, or menus.

When `rules` is non-empty, that function is skipped. Rules start from `WIN_MAYBE_OPTIONS_DEFAULT` (`dim = NAN`) and `win_maybe_options_fold` keeps `.dim = number_or_d(upper.dim, lower.dim)`. The unwrapped default in `win_options_from_config` is `.dim = 0`. A window stays undimmed until a rule sets `dim`. The condition target `focused` in `c2.c` is `w->a.map_state == XCB_MAP_STATE_VIEWABLE && w->is_focused`. It does **not** consult the wintype `focus` flag. `group_focused` is `w->is_group_focused`. A rule `match = "!focused"; dim = 0.25` therefore darkens a dock or a desktop window that is not the focused window. That is the documented way to get closer to LeDimmer. It still does not darken the root pixmap.

`is_focused` is `wm_focused_win(ps->wm) == w->tree_ref`. If `use_ewmh_active_win` is off, the frame event mask includes `XCB_EVENT_MASK_FOCUS_CHANGE`. The help string in `options.c` names the property `_NET_WM_ACTIVE_WINDOW`; the man page names `_NET_ACTIVE_WINDOW`. The EWMH atom is `_NET_ACTIVE_WINDOW` (below).

Paint, in `commands_for_window_body`:

```
double dim = layer->options.dim;
if (!inactive_dim_fixed)
    dim *= layer->opacity;
struct color tint = {.red = 1. - dim, .green = 1. - dim, .blue = 1. - dim, .alpha = 1.};
.tint = color_mult_alpha(tint, opacity),
```

`color_mult_alpha` multiplies every channel, including rgb, by the opacity. Opacity and dim meet in one tint, but they are different inputs: dim scales rgb toward 0, opacity scales the whole premul color.

GL `default_post_processing` does `c = c * tint`. The uniforms `opacity` and `dim` are kept "for backwards compatibility"; the comment says opacity is `tint.a` and dim is `(tint.r + tint.g + tint.b) / 3.0`. The default shader uses `tint`, not the `dim` uniform.

xrender `xrender_blit` fills `tint_pixel` (created with `componentalpha = 1`) with that tint and uses it as the mask of `XCB_RENDER_PICT_OP_SRC`, which multiplies the source by the mask. Alpha of the window is applied separately. Dim is a color multiply on this backend too, not a second opacity.

`command_for_shadow` sets `.tint = layer->shadow_color`. It does not use the dim tint. `command_builder_build` draws the root image with `BACKEND_COMMAND_COPY_AREA`, or a blit whose tint is `{1, 1, 1, 1}`. Inactive dim does not darken the wallpaper pixmap and does not darken shadows.

picom has to be the compositor to do any of this. See section g.

## c. xfwm4 compositor opacity — alpha only

Behavior: **translucency**. Not a brightness multiply. Not a sheet. Defaults do nothing, because every opacity key below is 100 except the shadow.

Opened on xfwm4 master:

- [`defaults/defaults`](https://gitlab.xfce.org/xfce/xfwm4/-/raw/master/defaults/defaults)
- [`src/client.h`](https://gitlab.xfce.org/xfce/xfwm4/-/raw/master/src/client.h)
- [`src/client.c`](https://gitlab.xfce.org/xfce/xfwm4/-/raw/master/src/client.c)
- [`src/compositor.c`](https://gitlab.xfce.org/xfce/xfwm4/-/raw/master/src/compositor.c)
- [`src/hints.h`](https://gitlab.xfce.org/xfce/xfwm4/-/raw/master/src/hints.h)
- [`src/netwm.c`](https://gitlab.xfce.org/xfce/xfwm4/-/raw/master/src/netwm.c)
- [`COMPOSITOR`](https://gitlab.xfce.org/xfce/xfwm4/-/raw/master/COMPOSITOR)

Defaults: `frame_opacity=100`, `inactive_opacity=100`, `move_opacity=100`, `popup_opacity=100`, `resize_opacity=100`, `shadow_opacity=50`, `use_compositing=true`, `unredirect_overlays=true`. The `COMPOSITOR` notes file is older: it says the compositor was disabled by default and documents turning it off with `xfwm4 --compositor=off`. The keys in `defaults/defaults` are the current ones.

`clientUpdateOpacity`:

```
opaque = (FLAG_TEST(c->type, WINDOW_TYPE_DONT_PLACE | WINDOW_TYPE_DONT_FOCUS)
          || (focused == c));
clientSetOpacity (c, c->opacity, OPACITY_INACTIVE, opaque ? 0 : OPACITY_INACTIVE);
```

`WINDOW_TYPE_DONT_PLACE` is `WINDOW_DESKTOP | WINDOW_DOCK | WINDOW_UTILITY | WINDOW_SPLASHSCREEN`. `WINDOW_TYPE_DONT_FOCUS` is `WINDOW_SPLASHSCREEN | WINDOW_DOCK`. Inactive opacity is not applied to desktop, dock, utility, or splash, and not applied to the focused client. It is applied to an unfocused normal window and to an unfocused dialog. `WINDOW_MENU`, `WINDOW_TOOLBAR`, and `WINDOW_NOTIFICATION` are not in that mask.

`clientSetOpacity`, when `OPACITY_INACTIVE` is set, does `multiplier *= inactive_opacity` and `divisor *= 100`, then `compositorWindowSetOpacity` on `c->frame`. `NET_WM_OPAQUE` is `G_MAXUINT32`.

`solid_picture(..., argb=FALSE)` uses `PictStandardA8`. The non-opaque path builds

```
cw->alphaPict = solid_picture (screen_info, FALSE,
                               (double) cw->opacity / NET_WM_OPAQUE,
                               0.0, 0.0, 0.0);
```

and `paint_win` composites with `PictOpOver` and that picture as the **mask**. `frame_opacity` builds the same kind of A8 `alphaBorderPict`. Red, green, and blue of the mask are 0; the only channel that changes coverage is alpha. That is not a darken.

`paint_root` copies `rootTile` with `PictOpSrc`. Shadows are a separate `PictOpOver` of `blackPicture` through `cw->shadow`, painted with the window in the bottom-to-top pass. `blackPicture` is the shadow color, not an inactive-dim veil.

`init_opacity`: a compositor window with no client and no opacity property gets `popup_opacity / 100.0 * NET_WM_OPAQUE`. That is how undecorated override-redirect popups pick up `popup_opacity`. At the default 100 they stay opaque.

Lowering `inactive_opacity` makes unfocused windows see-through. It leaves the desktop, the panel, and utility windows alone, and it does not darken the root tile. It does not match KWin or LeDimmer.

## d. A standalone X11 client — the LeDimmer analogue

Behavior, if the window is black, translucent, and stacked **under** the active frame: **black source-over** of everything below that frame, including the desktop. The active window's pixels are not touched. Windows stacked above the active frame stay bright. This is not per-window multiply, and it is not a hole.

This client must not be a compositor. It must not call `CompositeRedirectSubwindows`, `CompositeGetOverlayWindow`, or take `_NET_WM_CM_Sn`. An extra ARGB window is another redirected child. xfwm4, which already owns manual redirection, blends it.

### What the sheet is

One `override-redirect` window, depth 32, ARGB visual, covering the root. Fill it with black at a chosen alpha and composite it with `PictOpOver` (or let the compositor do that by putting the alpha in the window pixmap). Render's `Over` factors are `Fa = 1`, `Fb = 1 - Aa` ([renderproto](https://www.x.org/releases/X11R7.7/doc/renderproto/renderproto.txt), section 8). For premultiplied black that is a multiply of the destination rgb by `(1 - A)` and, on an opaque destination, alpha stays 1. The wallpaper and the windows under the sheet get darker. They do not become translucent.

Override-redirect is what keeps xfwm4 from framing it. EWMH §2.11 says override-redirect windows are ignored by traditional window managers and are still painted by the compositing manager ([wm spec](https://specifications.freedesktop.org/wm/latest-single/)).

### Click-through

The Shape spec: "the list of rectangles can be empty, specifying an empty region; this is not the same as passing `None`." The effective input region is the intersection of the client input region, the default input region, and the client bounding region ([shape.html](https://www.x.org/releases/current/doc/xextproto/shape.html)). An empty client input region leaves an empty effective input region, so the pointer falls through. `SetWindowShapeRegion` in the XFixes spec sets the Shape kind from an XFixes region ([fixesproto](https://www.x.org/releases/current/doc/fixesproto/fixesproto.txt)). xfwm4 does this to its own overlay: `XFixesCreateRegion(dpy, NULL, 0)` and `XFixesSetWindowShapeRegion(..., ShapeInput, ...)`. The bounding shape stays the full rectangle, so the pixels still show. The LeDimmer page does not mention click-through. On X it is required, or the sheet steals the clicks that should focus the dimmed windows.

### Who is active, and which sibling to stack against

EWMH §3.8: `_NET_ACTIVE_WINDOW` is "The window ID of the currently active window or None if no window has the focus." It is read-only, set by the window manager. A client that wants to activate a window sends a ClientMessage; a dimmer only reads the property.

That id is the client window. xfwm4 creates `c->frame` and `XReparentWindow`s `c->window` into it (`client.c`). `XConfigureWindow` stacks against a sibling. The sibling is the frame, the root's child, not the client. Walk `XQueryTree` until the parent is the root.

`CWSibling` + `CWStackMode` `Below` against that frame puts the sheet directly under the foreground window. That is the author's 1.0.0.0 sentence. Everything stacked lower is darkened, including the desktop window. The focused frame and everything above it stay bright.

When `_NET_ACTIVE_WINDOW` is `None`, or the active window is the desktop, restacking under that window would leave the desktop undimmed. LeDimmer 1.0.0.1 undimmed when the desktop was selected. LeDimmer 1.0.0.4 "dims the desktop even when focused." The 1.0.0.4 behavior is: keep the sheet mapped above the desktop in that case.

### A hole is a different behavior

A topmost click-through sheet whose bounding shape has the active frame's rectangle subtracted dims every pixel outside that rectangle, on every monitor, including panels and windows that are stacked above the active one. It also covers the shadow pixels that sit outside the frame. That is not what the author described. It is a later option if "everything except the focused rectangle" is required. Restack-under is the match for the text that was opened.

## e. The X server, including XLibre, is the wrong layer

Opened: [Composite protocol](https://www.x.org/releases/current/doc/compositeproto/compositeproto.txt), [RandR protocol](https://www.x.org/releases/current/doc/randrproto/randrproto.txt) `RRSetCrtcGamma`, [XLibre README](https://raw.githubusercontent.com/X11Libre/xserver/master/README.md) and the [repository front page](https://github.com/X11Libre/xserver).

Composite redirects a window hierarchy to off-screen storage when a client asks. `RedirectWindow` with `Manual` may be selected by only one client; another attempt is an Access error. The root itself may not be redirected (`Match`). The server does not know which window is "inactive", and it does not multiply inactive windows on its own. The EWMH compositing section says the compositing manager paints those pixmaps onto the screen.

`RRSetCrtcGamma` "sets the gamma ramps for 'crtc'." That changes the whole output, focused window included. It is not a per-window dim.

The XLibre README's selected features are TearFree, atomic modesetting, Nvidia driver versions, `Xnamespace`, seatd, `Xfbdev`, and CI. Per-window inactive dim is not among them. A search of that README did not find one. No XLibre source file that adds a per-window inactive dim was opened. Compositing on XLibre is still a client of the Composite extension, the same as on X.Org.

Drawing into the Composite overlay window is also the wrong layer. The overlay is always above normal windows, below the screensaver, override-redirect, absent from `QueryTree`, and reachable only through `CompositeGetOverlayWindow`. "Multiple clients may render to the Composite Overlay Window ... but the specific arbitration rules followed by these clients is not defined by this specification." xfwm4 already maps that window and redirects the root's children manually. A second client painting there has no defined arbitration with xfwm4.

## f. GTK cannot do this inside a normal application

Opened: [`gtk_widget_set_opacity`](https://docs.gtk.org/gtk3/method.Widget.set_opacity.html), [`GdkWindowTypeHint`](https://docs.gtk.org/gdk3/enum.WindowTypeHint.html), [`gdk_x11_window_set_type_hint`](https://gitlab.gnome.org/GNOME/gtk/-/raw/gtk-3-24/gdk/x11/gdkwindow-x11.c).

`gtk_widget_set_opacity` "request[s] the widget to be rendered partially transparent." For a toplevel, "On X11 this has any effect only on X screens with a compositing manager running." That is the widget's own alpha, which xfwm4 then applies as the A8 mask in section c. It does not multiply another client's pixels, and it does not cover the wallpaper.

GDK draws the windows of its own process. The Composite spec gives the redirected pixmaps of other clients to the compositing manager, not to an ordinary application. No GTK call that rescales other processes' windows was found in the pages opened.

A normal GTK toplevel is managed. xfwm4 reparents it into a frame. Putting a sheet under the focused frame requires override-redirect, an empty input shape, and restacking against that frame. Those are X11 requests. GTK is a reasonable toolkit for a later settings window or a tray icon. The sheet is not a `GtkWidget` opacity change.

## g. Only one compositor owns manual redirection

Behavior of running picom beside xfwm4's compositor: it does not work. One of them is refused.

Composite `RedirectWindow`: "Only one client may specify an update type of Manual, another attempt will result in an Access error." `RedirectSubwindows` redirects current and future children "as in RedirectWindow." If the update is Manual, parent background painting is inhibited.

EWMH §8.1: "For each screen they manage, compositing manager MUST acquire ownership of a selection named `_NET_WM_CM_Sn`, where `n` is the screen number."

xfwm4 `compositorManageScreen`: if `compositorCheckCMSelection` finds an owner of `_NET_WM_CM_S%d` or the older `COMPOSITING_MANAGER` atom, it warns "Another compositing manager is running on screen %i" and returns false. Otherwise `compositorSetCMSelection` takes `_NET_WM_CM_S%d` on `xfwm4_win`, gets the overlay, sets an empty `ShapeInput` on it, and calls `XCompositeRedirectSubwindows(..., CompositeRedirectManual)`.

picom `session_redirection_mode` returns `XCB_COMPOSITE_REDIRECT_MANUAL` when the backend presents to the screen. `register_cm` returns 1 if `_NET_WM_CM_S%d` already has an owner, otherwise it sets the selection. If a manual session sees `compositor_running`, it logs "Another composite manager is already running" and errors. `redirect_start` calls `xcb_composite_redirect_subwindows` and, on failure, "Another composite manager is already running (and does not handle `_NET_WM_CM_Sn` correctly)".

picom's `dim` therefore means turning xfwm4's compositor off (`xfwm4 --compositor=off` is the switch in the `COMPOSITOR` notes, and `use_compositing` is the xfconf/default key) and living with picom's shadows and fading. A client that only maps an ARGB window does not take the selection and does not redirect, so it can run while xfwm4's compositor stays on.

## The desk this has to live on

### Multi-monitor

One X screen is one root. RandR outputs are regions of that screen. LeDimmer 1.0.0.4 uses one window covering the screen; the 512×512 tiling in 1.0.0.3 is a Windows workaround the author replaced. On X, one sheet the size of the root, resized when the root size changes, is the same idea.

Stacking order is one list for the screen, not one list per monitor. A sheet placed under the active frame darkens what is below that frame everywhere. A window on another monitor that happens to be stacked above the active frame stays bright, and it covers the sheet on that monitor. A focused fullscreen window covers the sheet where the window covers the screen. The other monitor is dimmed only for pixels whose windows are below the sheet. A geometric hole in a topmost sheet is what would dim every output outside the focused rectangle. That is the optional behavior, not the first one.

EWMH `_NET_WM_FULLSCREEN_MONITORS` is a list of four monitor indices (top, bottom, left, right) for a fullscreen window that spans outputs. xfwm4's layer for fullscreen is separate from that property; see below.

### Panels

`xfce4-panel` `panel_window_new` sets `"type-hint", GDK_WINDOW_TYPE_HINT_DOCK` ([`panel-window.c`](https://gitlab.xfce.org/xfce/xfce4-panel/-/raw/master/panel/panel-window.c)). GDK's X11 backend writes `_NET_WM_WINDOW_TYPE_DOCK` for `GDK_WINDOW_TYPE_HINT_DOCK` ([`gdkwindow-x11.c`](https://gitlab.gnome.org/GNOME/gtk/-/raw/gtk-3-24/gdk/x11/gdkwindow-x11.c)). The hint's documentation says it is "Used for creating dock or panel windows."

EWMH: `_NET_WM_WINDOW_TYPE_DOCK` "indicates a dock or panel feature. Typically a Window Manager would keep such windows on top of all other windows."

xfwm4 `clientWindowType` sets `WINDOW_DOCK` and `initial_layer = WIN_LAYER_DOCK` for that atom. Layers in `hints.h`: `WIN_LAYER_DESKTOP` 0, `WIN_LAYER_NORMAL` 4, `WIN_LAYER_ONTOP` 6, `WIN_LAYER_DOCK` 8, `WIN_LAYER_FULLSCREEN` 10, `WIN_LAYER_ABOVE_DOCK` 12. A sheet under a normal frame sits under the dock layer, so the panel stays bright. KWin's default `DimPanels=false` does the same on purpose. xfwm4 also skips docks in `inactive_opacity` (`WINDOW_TYPE_DONT_FOCUS` includes `WINDOW_DOCK`).

If the sheet is raised above the panel, the panel is darkened and clicks still pass through only where the input shape is empty. That is usable for the panel's buttons, and it is not the LeDimmer "under the foreground window" placement.

### Desktop and icons

`xfce_desktop_constructed` calls `gtk_window_set_type_hint(..., GDK_WINDOW_TYPE_HINT_DESKTOP)` ([`xfce-desktop.c`](https://gitlab.xfce.org/xfce/xfdesktop/-/raw/master/src/xfce-desktop.c)). GDK writes `_NET_WM_WINDOW_TYPE_DESKTOP`. EWMH says that type "can include a single window containing desktop icons with the same dimensions as the screen." xfwm4 maps it to `WINDOW_DESKTOP` and `WIN_LAYER_DESKTOP`, and `clientUpdateOpacity` does not apply inactive opacity to it.

The sheet, placed under a normal frame and above this desktop window, darkens the wallpaper and the icons. That is the part KWin skips unless `DimDesktop` is true, and the part LeDimmer 1.0.0.4 keeps on even when the desktop is focused. picom's legacy `--inactive-dim` does not dim this window, because its wintype `focus` defaults to true. A picom rule that matches `!focused` can.

The root pixmap, if something paints it, is under the desktop window. xfwm4 copies it with `PictOpSrc` only in the damaged area not covered by opaque windows. The sheet does not need to special-case the root tile: the desktop window is what the user sees, and the sheet covers it.

### Override-redirect menus and tooltips

EWMH says `_NET_WM_WINDOW_TYPE_DROPDOWN_MENU`, `_NET_WM_WINDOW_TYPE_POPUP_MENU`, `_NET_WM_WINDOW_TYPE_TOOLTIP`, `_NET_WM_WINDOW_TYPE_NOTIFICATION`, `_NET_WM_WINDOW_TYPE_COMBO`, and `_NET_WM_WINDOW_TYPE_DND` are "typically used on override-redirect windows." Section 2.11: the compositing manager still paints them.

They are mapped above other windows. A sheet that moves only when `_NET_ACTIVE_WINDOW` changes, or when the focused frame is restacked, stays under those popups. The menu stays bright. KWin never dims `isPopupWindow()`. picom's legacy path treats tooltip, popup_menu, dropdown_menu, notification, combo, and dnd as always focused. Listening to every `SubstructureNotify` and restacking on each one would race with menus that map above the sheet and then get buried if the sheet is raised again. Restack when the active window or its frame changes, and leave the menu alone.

xfwm4's `popup_opacity` can make those windows translucent. It does not darken them. Default is 100.

### Shadows

xfwm4 paints a window's shadow in the same bottom-to-top pass as the window, with `PictOpOver` of `blackPicture` through the shadow picture. The shadow is not a separate X window. A sheet under the focused frame leaves that window's shadow above the sheet, so the shadow stays bright. Shadows of windows below the sheet are darkened with those windows.

KWin multiplies the `ShadowItem` because it is a child of the window item. picom does not put the dim tint on the shadow.

A bounding-shape hole cut to the frame rectangle covers the shadow that extends outside the frame. Restack-under does not.

### Dialogs and transients

EWMH: a managed window with `WM_TRANSIENT_FOR` and no `_NET_WM_WINDOW_TYPE` is a dialog. When the dialog is focused, `_NET_ACTIVE_WINDOW` is the dialog, and a sheet under that frame leaves the dialog bright and darkens the parent underneath it. That matches LeDimmer's "apart from the active window." It does not match KWin `DimByGroup=true`, which keeps `belongToSameGroup` windows bright, or picom `--detect-transient`, which treats the transient group as focused together.

If a transient stays stacked above its parent while the parent is the active window, restack-under leaves the transient bright, because it is above the sheet. Group tracking can wait.

xfwm4 does apply `inactive_opacity` to unfocused dialogs (`WINDOW_DIALOG` is not in the don't-place / don't-focus mask). That is still translucency.

### Fullscreen

EWMH `_NET_WM_STATE_FULLSCREEN`: the window "should fill the entire screen and have no window decorations." `_NET_WM_STATE_ABOVE`: "on top of most windows."

xfwm4 puts fullscreen clients on `WIN_LAYER_FULLSCREEN` (10), which is above `WIN_LAYER_DOCK` (8). A focused fullscreen window covers the sheet on the screen area it covers. Other monitors follow the global stack, as above.

Unredirect is narrower than "any fullscreen window", and it matters because it unmaps the overlay:

`map_win` unredirects a redirected, native-opaque, unshaped window when it is fullscreen (or something is already unredirected) and either `bypass_compositor == 1`, or `unredirect_overlays` is true and the window is override-redirect with `bypass_compositor == 0`. `unredirect_overlays` defaults to true. `unredirect_win` calls `XCompositeUnredirectWindow(..., CompositeRedirectManual)`. When an unredirected fullscreen window is mapped, `wins_unredirected` is incremented and, on the first one, the overlay is unmapped. When the last such window unmaps, the overlay is mapped again.

While the overlay is unmapped, xfwm4 is not presenting the redirected scene, so the sheet is not on screen. A fullscreen client that sets `_NET_WM_BYPASS_COMPOSITOR` to 1 does that to the whole screen, not only to its own rectangle. A normal fullscreen window that does not request bypass stays redirected, and the sheet remains under it. picom has the same class of switch: `--unredir-if-possible`, and the rule key `unredir`. Treat bypass-unredirect as a limit of every X11 compositor, not as something the sheet can paint through.

### Keep-above

KWin skips `keepAbove()` unless `DimKeepAbove` is set. xfwm4's `WIN_LAYER_ONTOP` (6) is above `WIN_LAYER_NORMAL` (4) and below `WIN_LAYER_DOCK`. A keep-above window stacked above the active normal frame stays above a sheet that is under that frame, so it stays bright. That is the same stacking result as KWin's default, arrived at by z-order rather than by a type check.

## What to build

Build the smallest LeDimmer, not a KWin port and not a compositor.

One X11 client. Leave xfwm4's compositor on. Do not run picom. Do not lower `inactive_opacity`. Do not patch XLibre. Do not try to dim from GTK.

Map one override-redirect ARGB window the size of the root, filled with black at a chosen alpha. Set its input shape to the empty region so clicks pass through. When `_NET_ACTIVE_WINDOW`, the frame's geometry, or the frame's stacking changes, restack the sheet `Below` the active client's frame (the root child, after `XQueryTree`). That darkens the desktop, the icons, and every window under the focused frame, and it leaves the focused window's pixels and its xfwm4 shadow alone. When the active window is `None` or a `_NET_WM_WINDOW_TYPE_DESKTOP`, keep the sheet up, above the desktop, which is LeDimmer 1.0.0.4 rather than 1.0.0.1.

The panel, keep-above windows, and override-redirect menus stay bright because they are stacked above a normal frame. That keeps the panel and menus usable. It is the behavior of "a black transparent fullscreen window under the foreground window." A hole cut in a topmost sheet, so that panels and other monitors darken too, is a later change. It also darkens the focused window's shadow, which restack-under does not.

The author's page does not publish flag names or a default alpha. Do not copy a number from reviews. Expose the alpha as a parameter. A tray icon can be GTK later. The sheet process should not be a GTK window.

picom with `rules` and `dim`, and xfwm4 compositing off, is a way to preview per-window multiply, including a rule that dims the desktop window. It replaces the compositor this desk is keeping. A change inside `paint_win` to scale color, the way `multiplyBrightness` does, would be the xfwm4-native version of KWin. Both are larger than the sheet, and both wait until the sheet's stacking is shown to be wrong.
