#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>

#include "decide.h"

/* LeDimmer's page never published a default. 96 is about 38% black. */
enum { DEFAULT_ALPHA = 96 };
enum { LISTEN_BACKLOG = 8 };

static Display *dpy;
static int scr;
static Window root;
static Window sheet;
static Picture pict;
static int sw;
static int sh;
static int strength = DEFAULT_ALPHA;
static int red;
static int green;
static int blue;
static int visible = 1;
static int no_tray;
static Window active_frame;
static int need_place;
static int quit_flag;
static int listen_fd = -1;
static int bound;
static char dir_path[96];
static char sock_path[108];
static char cfg_path[108];

static Atom net_active;
static Atom net_client_list;
static Atom net_wm_window_type;
static Atom net_wm_window_type_desktop;

static void
say(const char *s)
{
	size_t n = 0u;
	ssize_t w;

	if (s == 0) {
		return;
	}
	while (s[n] != '\0') {
		n = n + 1u;
	}
	if (n > 0u) {
		w = write(STDERR_FILENO, s, n);
		if (w < 0) {
			w = 0;
		} else {
			w = w;
		}
	} else {
		n = 0u;
	}
}

static void
say_u(unsigned v)
{
	char rev[10];
	char out[11];
	int n = 0;
	int i;

	if (v == 0u) {
		say("0");
	} else {
		while ((v > 0u) && (n < 10)) {
			rev[n] = (char)('0' + (int)(v % 10u));
			n = n + 1;
			v = v / 10u;
		}
		for (i = 0; i < n; i = i + 1) {
			out[i] = rev[n - 1 - i];
		}
		out[n] = '\0';
		say(out);
	}
}

static void
say_hex2(int v)
{
	static const char hex[] = "0123456789abcdef";
	char out[3];

	if ((v < 0) || (v > 255)) {
		v = 0;
	} else {
		v = v;
	}
	out[0] = hex[(v >> 4) & 15];
	out[1] = hex[v & 15];
	out[2] = '\0';
	say(out);
}

static void
usage(int fd)
{
	static const char text[] =
		"usage: litedim [--alpha N] [--color RRGGBB] [--no-tray]\n"
		"\n"
		"Dim everything stacked under the focused window.\n"
		"N is the coverage, 0 through 255. The default is 96.\n"
		"RRGGBB tints the sheet. The default is black, 000000.\n"
		"0 leaves the sheet unmapped.\n"
		"The tray icon and the settings window are part of this program.\n"
		"--no-tray does not create them.\n";
	ssize_t w;

	w = write(fd, text, sizeof text - 1u);
	if (w < 0) {
		w = 0;
	} else {
		w = w;
	}
}

static int
on_xerror(Display *d, XErrorEvent *e)
{
	char buf[64];

	if ((e->error_code == BadWindow) || (e->error_code == BadDrawable) ||
	    (e->error_code == BadMatch)) {
		return 0;
	} else {
		XGetErrorText(d, (int)e->error_code, buf, (int)sizeof buf);
		say("litedim: X error: ");
		say(buf);
		say("\n");
		return 0;
	}
}

static int
copy_cap(char *dst, int cap, const char *src)
{
	int i = 0;

	if ((dst == 0) || (src == 0) || (cap < 1)) {
		return -1;
	}
	while (src[i] != '\0') {
		if (i >= (cap - 1)) {
			return -1;
		}
		dst[i] = src[i];
		i = i + 1;
	}
	dst[i] = '\0';
	return i;
}

static int
join_path(char *dst, int cap, const char *dir, const char *name)
{
	int n;
	int i = 0;

	n = copy_cap(dst, cap, dir);
	if (n < 0) {
		return -1;
	}
	if (n >= (cap - 1)) {
		return -1;
	}
	dst[n] = '/';
	n = n + 1;
	while (name[i] != '\0') {
		if (n >= (cap - 1)) {
			return -1;
		}
		dst[n] = name[i];
		n = n + 1;
		i = i + 1;
	}
	dst[n] = '\0';
	return n;
}

static int
set_cloexec(int fd)
{
	int flags;
	int ok = 1;

	flags = fcntl(fd, F_GETFD);
	if (flags < 0) {
		ok = 0;
	} else if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
		ok = 0;
	} else {
		ok = 1;
	}
	return ok;
}

static int
set_nonblock(int fd)
{
	int flags;
	int ok = 1;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) {
		ok = 0;
	} else if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		ok = 0;
	} else {
		ok = 1;
	}
	return ok;
}

static int
build_paths(void)
{
	char rev[16];
	char num[16];
	unsigned uid;
	int n = 0;
	int i;
	int at;
	int ok = 1;

	if (copy_cap(dir_path, (int)sizeof dir_path, "/tmp/litedim-") < 0) {
		return 0;
	}
	uid = (unsigned)getuid();
	if (uid == 0u) {
		rev[0] = '0';
		n = 1;
	} else {
		while ((uid > 0u) && (n < 10)) {
			rev[n] = (char)('0' + (int)(uid % 10u));
			n = n + 1;
			uid = uid / 10u;
		}
	}
	at = 0;
	for (i = n - 1; i >= 0; i = i - 1) {
		num[at] = rev[i];
		at = at + 1;
	}
	num[at] = '\0';
	at = 0;
	while (dir_path[at] != '\0') {
		at = at + 1;
	}
	i = 0;
	while (num[i] != '\0') {
		if (at >= ((int)sizeof dir_path - 1)) {
			ok = 0;
		} else {
			dir_path[at] = num[i];
			at = at + 1;
		}
		i = i + 1;
	}
	dir_path[at] = '\0';
	if (ok == 0) {
		return 0;
	}
	if (join_path(sock_path, (int)sizeof sock_path, dir_path, "control") < 0) {
		return 0;
	}
	if (join_path(cfg_path, (int)sizeof cfg_path, dir_path, "config") < 0) {
		return 0;
	}
	return 1;
}

static int
ensure_dir(void)
{
	struct stat st;
	int ok = 1;

	if (mkdir(dir_path, S_IRWXU) != 0) {
		if (errno != EEXIST) {
			say("litedim: cannot create the control directory\n");
			ok = 0;
		} else {
			ok = 1;
		}
	} else {
		ok = 1;
	}
	if (ok == 0) {
		return 0;
	}
	if (lstat(dir_path, &st) != 0) {
		say("litedim: cannot stat the control directory\n");
		return 0;
	}
	if ((S_ISDIR(st.st_mode) == 0) || (st.st_uid != getuid())) {
		say("litedim: control directory is not ours\n");
		return 0;
	}
	if (chmod(dir_path, S_IRWXU) != 0) {
		say("litedim: cannot set the control directory mode\n");
		return 0;
	}
	return 1;
}

static void
apply_config_cmd(const cmd *c)
{
	if (c->kind == CMD_ALPHA) {
		strength = c->alpha;
		if (c->alpha == 0) {
			visible = 0;
		} else {
			strength = c->alpha;
		}
	} else if (c->kind == CMD_COLOR) {
		red = c->red;
		green = c->green;
		blue = c->blue;
	} else if (c->kind == CMD_VISIBLE) {
		if (c->visible == 0) {
			visible = 0;
		} else {
			visible = 1;
		}
	} else if (c->kind == CMD_SHOW) {
		visible = 1;
	} else if (c->kind == CMD_HIDE) {
		visible = 0;
	} else {
		c = c;
	}
}

static void
load_config(void)
{
	char buf[256];
	int fd;
	ssize_t n;
	int i = 0;
	int start = 0;

	fd = open(cfg_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		return;
	}
	n = read(fd, buf, sizeof buf);
	if (close(fd) != 0) {
		n = n;
	} else {
		n = n;
	}
	if (n <= 0) {
		return;
	}
	while (i <= (int)n) {
		if ((i == (int)n) || (buf[i] == '\n')) {
			int len = i - start;
			if ((len > 0) && (len <= DECIDE_LINE_MAX)) {
				cmd c = parse_line(buf + start, len);
				apply_config_cmd(&c);
			} else {
				len = len;
			}
			start = i + 1;
		} else {
			start = start;
		}
		i = i + 1;
	}
}

static int
save_config(void)
{
	char body[96];
	int n;
	int fd;
	int off = 0;
	ssize_t w;
	int ok = 1;

	n = format_state(body, (int)sizeof body, strength, red, green, blue, visible);
	if (n < 0) {
		return 0;
	}
	fd = open(cfg_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
		S_IRUSR | S_IWUSR);
	if (fd < 0) {
		return 0;
	}
	if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
		close(fd);
		return 0;
	}
	while ((off < n) && (ok == 1)) {
		w = write(fd, body + off, (size_t)(n - off));
		if (w < 0) {
			if (errno == EINTR) {
				ok = 1;
			} else {
				ok = 0;
			}
		} else {
			off = off + (int)w;
		}
	}
	if (close(fd) != 0) {
		ok = 0;
	} else {
		ok = ok;
	}
	return ok;
}

static int
daemon_alive(void)
{
	int fd;
	struct sockaddr_un addr;
	struct pollfd pfd;
	int err = 0;
	socklen_t elen = (socklen_t)sizeof err;
	char req[] = "status\n";
	char buf[8];
	ssize_t n;
	int pr;
	int alive = 0;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return 0;
	}
	if (set_cloexec(fd) == 0) {
		close(fd);
		return 0;
	}
	if (set_nonblock(fd) == 0) {
		close(fd);
		return 0;
	}
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	if (copy_cap(addr.sun_path, (int)sizeof addr.sun_path, sock_path) < 0) {
		close(fd);
		return 0;
	}
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		if (errno != EINPROGRESS) {
			close(fd);
			return 0;
		}
		pfd.fd = fd;
		pfd.events = POLLOUT;
		pr = poll(&pfd, 1, 300);
		if (pr <= 0) {
			close(fd);
			return 0;
		}
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0) {
			close(fd);
			return 0;
		}
		if (err != 0) {
			close(fd);
			return 0;
		}
	}
	n = write(fd, req, sizeof req - 1u);
	if (n != (ssize_t)(sizeof req - 1u)) {
		close(fd);
		return 0;
	}
	pfd.fd = fd;
	pfd.events = POLLIN;
	pr = poll(&pfd, 1, 300);
	if (pr > 0) {
		n = read(fd, buf, sizeof buf);
		if (n > 0) {
			alive = 1;
		} else {
			alive = 0;
		}
	} else {
		alive = 0;
	}
	close(fd);
	return alive;
}

static int
listen_bind(void)
{
	struct sockaddr_un addr;
	struct stat st;
	int fd;
	int ok = 1;

	if (lstat(sock_path, &st) == 0) {
		if (S_ISSOCK(st.st_mode) != 0) {
			if (unlink(sock_path) != 0) {
				say("litedim: cannot replace the control socket\n");
				ok = 0;
			} else {
				ok = 1;
			}
		} else {
			say("litedim: control path is not a socket\n");
			ok = 0;
		}
	} else {
		if (errno != ENOENT) {
			say("litedim: cannot stat the control socket\n");
			ok = 0;
		} else {
			ok = 1;
		}
	}
	if (ok == 0) {
		return 0;
	}
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		say("litedim: cannot open the control socket\n");
		return 0;
	}
	if (set_cloexec(fd) == 0) {
		close(fd);
		say("litedim: cannot set the control socket mode\n");
		return 0;
	}
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	if (copy_cap(addr.sun_path, (int)sizeof addr.sun_path, sock_path) < 0) {
		close(fd);
		return 0;
	}
	if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		say("litedim: cannot bind the control socket\n");
		close(fd);
		return 0;
	}
	bound = 1;
	if (listen(fd, LISTEN_BACKLOG) != 0) {
		say("litedim: cannot listen on the control socket\n");
		close(fd);
		listen_fd = -1;
		return 0;
	}
	if (set_nonblock(fd) == 0) {
		close(fd);
		return 0;
	}
	listen_fd = fd;
	return 1;
}

static void
cleanup_listen(void)
{
	int ur;

	if (listen_fd >= 0) {
		close(listen_fd);
		listen_fd = -1;
	} else {
		listen_fd = -1;
	}
	if (bound != 0) {
		ur = unlink(sock_path);
		if (ur != 0) {
			ur = 0;
		} else {
			ur = 0;
		}
		bound = 0;
	} else {
		bound = 0;
	}
}

static int
compositor_present(void)
{
	char name[32];
	int n;
	unsigned scr_u;
	Atom sel;
	int present = 0;

	if (scr < 0) {
		return 0;
	}
	n = copy_cap(name, (int)sizeof name, "_NET_WM_CM_S");
	if (n < 0) {
		return 0;
	}
	scr_u = (unsigned)scr;
	if (scr_u >= 100u) {
		return 0;
	}
	if (scr_u >= 10u) {
		if (n >= ((int)sizeof name - 2)) {
			return 0;
		}
		name[n] = (char)('0' + (int)(scr_u / 10u));
		n = n + 1;
		scr_u = scr_u % 10u;
	} else {
		scr_u = scr_u;
	}
	if (n >= ((int)sizeof name - 1)) {
		return 0;
	}
	name[n] = (char)('0' + (int)scr_u);
	n = n + 1;
	name[n] = '\0';
	sel = XInternAtom(dpy, name, False);
	if (XGetSelectionOwner(dpy, sel) != None) {
		present = 1;
	} else {
		present = 0;
	}
	return present;
}

static Visual *
find_argb_visual(void)
{
	XVisualInfo tpl;
	XVisualInfo *info;
	int n = 0;
	int i;
	Visual *found = 0;

	tpl.screen = scr;
	tpl.depth = 32;
	tpl.class = TrueColor;
	info = XGetVisualInfo(dpy,
		VisualScreenMask | VisualDepthMask | VisualClassMask, &tpl, &n);
	if (info == 0) {
		return 0;
	}
	for (i = 0; i < n; i = i + 1) {
		XRenderPictFormat *fmt = XRenderFindVisualFormat(dpy, info[i].visual);
		if ((fmt != 0) && (fmt->type == PictTypeDirect) && (fmt->direct.alphaMask != 0)) {
			found = info[i].visual;
			break;
		}
	}
	XFree(info);
	return found;
}

static void
paint(void)
{
	XRenderColor c;
	uint16_t cr;
	uint16_t cg;
	uint16_t cb;
	uint16_t ca;

	if ((visible == 0) || (pict == 0)) {
		return;
	}
	premultiply(red, green, blue, strength, &cr, &cg, &cb, &ca);
	c.red = cr;
	c.green = cg;
	c.blue = cb;
	c.alpha = ca;
	XRenderFillRectangle(dpy, PictOpSrc, pict, &c, 0, 0, (unsigned)sw, (unsigned)sh);
}

static void
click_through(void)
{
	/*
	 * An empty region is not the same as passing None. None would restore
	 * the default input shape, and the sheet would take clicks.
	 */
	XserverRegion empty = XFixesCreateRegion(dpy, 0, 0);
	XFixesSetWindowShapeRegion(dpy, sheet, ShapeInput, 0, 0, empty);
	XFixesDestroyRegion(dpy, empty);
}

static int
root_size(void)
{
	Window r;
	int x;
	int y;
	unsigned w;
	unsigned h;
	unsigned b;
	unsigned d;
	int ok = 1;

	if (XGetGeometry(dpy, root, &r, &x, &y, &w, &h, &b, &d) == 0) {
		say("litedim: cannot read the root window size\n");
		ok = 0;
	} else {
		sw = (int)w;
		sh = (int)h;
		if (sw < 1) {
			sw = 1;
		} else {
			sw = sw;
		}
		if (sh < 1) {
			sh = 1;
		} else {
			sh = sh;
		}
	}
	return ok;
}

static Window *
root_children(unsigned *n)
{
	Window r;
	Window p;
	Window *kids = 0;
	unsigned nk = 0u;

	if (XQueryTree(dpy, root, &r, &p, &kids, &nk) == 0) {
		*n = 0u;
		return 0;
	}
	*n = nk;
	return kids;
}

static int
index_of(Window *kids, unsigned n, Window w)
{
	unsigned i;
	int found = -1;

	for (i = 0u; i < n; i = i + 1u) {
		if (kids[i] == w) {
			found = (int)i;
			break;
		}
	}
	return found;
}

/* _NET_ACTIVE_WINDOW is the client. xfwm4's frame is the child of the root. */
static Window
toplevel(const Window start)
{
	int i;
	Window found = 0;
	Window w = start;

	for (i = 0; (i < 16) && (w != 0) && (w != root); i = i + 1) {
		Window r;
		Window p;
		Window *ch = 0;
		unsigned n = 0u;

		if (XQueryTree(dpy, w, &r, &p, &ch, &n) == 0) {
			found = 0;
			break;
		}
		if (ch != 0) {
			XFree(ch);
		} else {
			ch = 0;
		}
		if (p == root) {
			found = w;
			break;
		}
		w = p;
	}
	return found;
}

static int
window_is_desktop(const Window w)
{
	Atom actual;
	int format;
	unsigned long n;
	unsigned long left;
	unsigned char *data = 0;
	int yes = 0;
	unsigned long i;

	if (w == 0) {
		return 0;
	}
	if (XGetWindowProperty(dpy, w, net_wm_window_type, 0, 16, False, XA_ATOM,
			&actual, &format, &n, &left, &data) != Success) {
		return 0;
	}
	if ((data != 0) && (format == 32)) {
		unsigned long *atoms = (unsigned long *)data;
		for (i = 0u; i < n; i = i + 1u) {
			if (atoms[i] == net_wm_window_type_desktop) {
				yes = 1;
			}
		}
	} else {
		yes = 0;
	}
	if (data != 0) {
		XFree(data);
	} else {
		data = 0;
	}
	return yes;
}

static Window
active_window(void)
{
	Atom actual;
	int format;
	unsigned long n;
	unsigned long left;
	unsigned char *data = 0;
	Window w = 0;

	/* The property has two values. Element 0 is the window. */
	if ((XGetWindowProperty(dpy, root, net_active, 0, 1, False, XA_WINDOW,
			&actual, &format, &n, &left, &data) == Success) &&
	    (data != 0) && (n >= 1u) && (format == 32)) {
		w = (Window)((unsigned long *)data)[0];
	} else {
		w = 0;
	}
	if (data != 0) {
		XFree(data);
	} else {
		data = 0;
	}
	if (w == sheet) {
		w = 0;
	} else {
		w = w;
	}
	return w;
}

static Window
find_desktop_frame(void)
{
	Atom actual;
	int format;
	unsigned long n;
	unsigned long left;
	unsigned long i;
	unsigned char *data = 0;
	Window found = 0;

	if ((XGetWindowProperty(dpy, root, net_client_list, 0, 4096, False, XA_WINDOW,
			&actual, &format, &n, &left, &data) != Success) ||
	    (data == 0) || (format != 32)) {
		if (data != 0) {
			XFree(data);
		} else {
			data = 0;
		}
		return 0;
	}
	for (i = 0u; i < n; i = i + 1u) {
		Window client = (Window)((unsigned long *)data)[i];
		if (window_is_desktop(client) != 0) {
			found = toplevel(client);
			break;
		}
	}
	XFree(data);
	return found;
}

static void
stack_against(Window sibling, int mode)
{
	XWindowChanges ch;

	if ((sibling == 0) || (sibling == sheet)) {
		return;
	}
	ch.sibling = sibling;
	ch.stack_mode = mode;
	XConfigureWindow(dpy, sheet, CWSibling | CWStackMode, &ch);
}

static void
place(void)
{
	Window *kids;
	unsigned n = 0u;
	int sheet_at;
	int frame_at = -1;
	int desk_at = -1;
	int have_frame = 0;
	place_act act;
	Window client;
	Window frame;
	Window desk;

	if (visible == 0) {
		return;
	}
	kids = root_children(&n);
	if (kids == 0) {
		return;
	}
	if (n > (unsigned)DECIDE_MAX_CHILDREN) {
		XLowerWindow(dpy, sheet);
		XFree(kids);
		return;
	}
	sheet_at = index_of(kids, n, sheet);
	client = active_window();
	if ((client != 0) && (window_is_desktop(client) == 0)) {
		frame = toplevel(client);
		active_frame = frame;
		if (frame != 0) {
			frame_at = index_of(kids, n, frame);
			if (frame_at >= 0) {
				have_frame = 1;
			} else {
				have_frame = 0;
			}
		} else {
			have_frame = 0;
		}
	} else {
		active_frame = 0;
		have_frame = 0;
	}
	if (have_frame == 0) {
		desk = find_desktop_frame();
		if (desk != 0) {
			desk_at = index_of(kids, n, desk);
		} else {
			desk_at = -1;
		}
	} else {
		desk_at = -1;
	}
	act = decide_place((int)n, sheet_at, frame_at, desk_at, have_frame);
	if (place_sibling_ok(&act, (int)n) == 0) {
		act.kind = PLACE_NONE;
	} else {
		act.kind = act.kind;
	}
	if (act.kind == PLACE_BELOW) {
		stack_against(kids[act.sibling], Below);
	} else if (act.kind == PLACE_ABOVE) {
		stack_against(kids[act.sibling], Above);
	} else if (act.kind == PLACE_LOWER) {
		XLowerWindow(dpy, sheet);
	} else {
		act.kind = PLACE_NONE;
	}
	XFree(kids);
}

static void
show_sheet(void)
{
	visible = 1;
	XMapWindow(dpy, sheet);
	paint();
	XSync(dpy, False);
	place();
}

static void
hide_sheet(void)
{
	visible = 0;
	active_frame = 0;
	XUnmapWindow(dpy, sheet);
}

static int
create_sheet(void)
{
	XSetWindowAttributes attr;
	XRenderPictFormat *fmt;
	Colormap cmap;
	Visual *argb;
	XClassHint class_hint;
	int ok = 1;

	argb = find_argb_visual();
	if (argb == 0) {
		say("litedim: no 32-bit ARGB visual\n");
		return 0;
	}
	fmt = XRenderFindVisualFormat(dpy, argb);
	if (fmt == 0) {
		say("litedim: no Render format for the ARGB visual\n");
		return 0;
	}
	cmap = XCreateColormap(dpy, root, argb, AllocNone);
	memset(&attr, 0, sizeof attr);
	attr.override_redirect = True;
	attr.colormap = cmap;
	attr.background_pixmap = None;
	attr.border_pixel = 0;
	attr.event_mask = ExposureMask;
	sheet = XCreateWindow(dpy, root, 0, 0, (unsigned)sw, (unsigned)sh, 0,
		32, InputOutput, argb,
		CWOverrideRedirect | CWColormap | CWBackPixmap | CWBorderPixel | CWEventMask,
		&attr);
	if (sheet == 0) {
		say("litedim: cannot create the sheet\n");
		ok = 0;
	} else {
		class_hint.res_name = "litedim";
		class_hint.res_class = "Litedim";
		XSetClassHint(dpy, sheet, &class_hint);
		XStoreName(dpy, sheet, "litedim");
		pict = XRenderCreatePicture(dpy, sheet, fmt, 0, 0);
		if (pict == 0) {
			say("litedim: cannot create a Render picture\n");
			ok = 0;
		} else {
			click_through();
			paint();
		}
	}
	return ok;
}

static int ui_handle(XEvent *ev);

static void
handle(XEvent *ev)
{
	if (ui_handle(ev) != 0) {
		return;
	}
	if (ev->type == Expose) {
		if ((ev->xexpose.window == sheet) && (ev->xexpose.count == 0)) {
			paint();
		} else {
			ev = ev;
		}
	} else if (ev->type == ConfigureNotify) {
		XConfigureEvent *c = &ev->xconfigure;
		if (c->window == root) {
			if ((c->width != sw) || (c->height != sh)) {
				sw = c->width;
				sh = c->height;
				if (sw < 1) {
					sw = 1;
				} else {
					sw = sw;
				}
				if (sh < 1) {
					sh = 1;
				} else {
					sh = sh;
				}
				XResizeWindow(dpy, sheet, (unsigned)sw, (unsigned)sh);
				paint();
				need_place = 1;
			} else {
				need_place = need_place;
			}
		} else if ((c->window == sheet) || ((active_frame != 0) && (c->window == active_frame))) {
			need_place = 1;
		} else {
			need_place = need_place;
		}
	} else if (ev->type == PropertyNotify) {
		if ((ev->xproperty.window == root) && (ev->xproperty.atom == net_active)) {
			need_place = 1;
		} else {
			need_place = need_place;
		}
	} else if ((ev->type == MapNotify) || (ev->type == UnmapNotify) ||
		(ev->type == DestroyNotify)) {
		Window w = ev->xmap.window;
		if ((w == sheet) || ((active_frame != 0) && (w == active_frame))) {
			need_place = 1;
		} else {
			need_place = need_place;
		}
	} else {
		ev = ev;
	}
}

static int
read_line(int fd, char *buf, int cap)
{
	int n = 0;
	struct pollfd pfd;
	int pr;

	pfd.fd = fd;
	pfd.events = POLLIN;
	while (n < cap) {
		char b;
		ssize_t r;

		pr = poll(&pfd, 1, 200);
		if (pr <= 0) {
			break;
		}
		r = read(fd, &b, 1);
		if (r == 0) {
			break;
		} else if (r < 0) {
			if (errno == EINTR) {
				r = 0;
			} else {
				break;
			}
		} else {
			if (b == '\n') {
				break;
			}
			buf[n] = b;
			n = n + 1;
		}
	}
	return n;
}

static void
reply_status(int fd)
{
	char body[96];
	int n;
	ssize_t w;
	int off = 0;

	n = format_state(body, (int)sizeof body, strength, red, green, blue, visible);
	if (n <= 0) {
		return;
	}
	while (off < n) {
		w = write(fd, body + off, (size_t)(n - off));
		if (w < 0) {
			if (errno == EINTR) {
				w = 0;
			} else {
				break;
			}
		} else {
			off = off + (int)w;
		}
	}
}

static void ui_refresh(void);

static void
apply_cmd(const cmd *c)
{
	if (c->kind == CMD_ALPHA) {
		if (c->alpha == 0) {
			strength = 0;
			hide_sheet();
		} else {
			strength = c->alpha;
			if (visible != 0) {
				paint();
			} else {
				show_sheet();
			}
		}
		if (save_config() == 0) {
			say("litedim: cannot save the settings\n");
		} else {
			strength = strength;
		}
	} else if (c->kind == CMD_COLOR) {
		red = c->red;
		green = c->green;
		blue = c->blue;
		if (visible != 0) {
			paint();
		} else {
			visible = 0;
		}
		if (save_config() == 0) {
			say("litedim: cannot save the settings\n");
		} else {
			red = red;
		}
	} else if (c->kind == CMD_SHOW) {
		show_sheet();
		if (save_config() == 0) {
			say("litedim: cannot save the settings\n");
		} else {
			visible = visible;
		}
	} else if (c->kind == CMD_HIDE) {
		hide_sheet();
		if (save_config() == 0) {
			say("litedim: cannot save the settings\n");
		} else {
			visible = visible;
		}
	} else if (c->kind == CMD_VISIBLE) {
		if (c->visible == 0) {
			hide_sheet();
		} else {
			show_sheet();
		}
		if (save_config() == 0) {
			say("litedim: cannot save the settings\n");
		} else {
			visible = visible;
		}
	} else if (c->kind == CMD_QUIT) {
		quit_flag = 1;
	} else {
		c = c;
	}
	if (c->kind != CMD_QUIT) {
		ui_refresh();
	} else {
		quit_flag = 1;
	}
}

static void
serve_once(void)
{
	int i;

	for (i = 0; i < LISTEN_BACKLOG; i = i + 1) {
		int fd;
		char buf[DECIDE_LINE_MAX];
		int n;
		cmd c;

		fd = accept(listen_fd, 0, 0);
		if (fd < 0) {
			if (errno == EINTR) {
				fd = -1;
			} else {
				break;
			}
		} else {
			if (set_cloexec(fd) == 0) {
				close(fd);
			} else {
				n = read_line(fd, buf, DECIDE_LINE_MAX);
				c = parse_line(buf, n);
				if (c.kind == CMD_STATUS) {
					reply_status(fd);
				} else if (c.kind == CMD_BAD) {
					c.kind = CMD_BAD;
				} else {
					apply_cmd(&c);
				}
				close(fd);
			}
		}
	}
}

enum { ICON_SIZE = 22 };
enum { DLG_W = 280 };
enum { DLG_H = 230 };
enum { SLIDER_X = 16 };
enum { SLIDER_W = 248 };
enum { SLIDER_H = 16 };
enum { LEVEL_Y = 36 };
enum { RED_Y = 78 };
enum { GREEN_Y = 114 };
enum { BLUE_Y = 150 };
enum { PREVIEW_X = 16 };
enum { PREVIEW_Y = 182 };
enum { PREVIEW_S = 32 };
enum { HIDE_X = 56 };
enum { QUIT_X = 164 };
enum { BTN_Y = 182 };
enum { BTN_W = 100 };
enum { BTN_H = 32 };
enum { DRAG_NONE = 0 };
enum { DRAG_LEVEL = 1 };
enum { DRAG_RED = 2 };
enum { DRAG_GREEN = 3 };
enum { DRAG_BLUE = 4 };

static Window icon;
static Window dialog;
static GC ui_gc;
static XFontStruct *ui_font;
static int dialog_up;
static int icon_embedded;
static int dock_sent;
static int drag;
static Atom atom_tray_op;
static Atom atom_xembed;
static Atom atom_xembed_info;
static Atom atom_wm_delete;
static Atom atom_wm_protocols;

static int
mask_shift(unsigned long mask)
{
	int shift = 0;

	while (((mask & 1u) == 0u) && (shift < 31)) {
		mask = mask >> 1;
		shift = shift + 1;
	}
	return shift;
}

static int
mask_bits(unsigned long mask)
{
	int shift;
	int bits = 0;

	shift = mask_shift(mask);
	mask = mask >> shift;
	while (((mask & 1u) != 0u) && (bits < 16)) {
		mask = mask >> 1;
		bits = bits + 1;
	}
	return bits;
}

static unsigned long
pixel_of(int r, int g, int b)
{
	Visual *vis;
	int rs;
	int gs;
	int bs;
	int rb;
	int gb;
	int bb;
	unsigned long pix;

	if (r < 0) {
		r = 0;
	} else if (r > 255) {
		r = 255;
	} else {
		r = r;
	}
	if (g < 0) {
		g = 0;
	} else if (g > 255) {
		g = 255;
	} else {
		g = g;
	}
	if (b < 0) {
		b = 0;
	} else if (b > 255) {
		b = 255;
	} else {
		b = b;
	}
	vis = DefaultVisual(dpy, scr);
	if ((vis->class != TrueColor) && (vis->class != DirectColor)) {
		if ((r + g + b) >= 384) {
			return WhitePixel(dpy, scr);
		}
		return BlackPixel(dpy, scr);
	}
	rs = mask_shift(vis->red_mask);
	gs = mask_shift(vis->green_mask);
	bs = mask_shift(vis->blue_mask);
	rb = mask_bits(vis->red_mask);
	gb = mask_bits(vis->green_mask);
	bb = mask_bits(vis->blue_mask);
	pix = 0u;
	if (rb >= 8) {
		pix = pix | ((unsigned long)r << (rb - 8)) << rs;
	} else if (rb > 0) {
		pix = pix | ((unsigned long)r >> (8 - rb)) << rs;
	} else {
		pix = pix;
	}
	if (gb >= 8) {
		pix = pix | ((unsigned long)g << (gb - 8)) << gs;
	} else if (gb > 0) {
		pix = pix | ((unsigned long)g >> (8 - gb)) << gs;
	} else {
		pix = pix;
	}
	if (bb >= 8) {
		pix = pix | ((unsigned long)b << (bb - 8)) << bs;
	} else if (bb > 0) {
		pix = pix | ((unsigned long)b >> (8 - bb)) << bs;
	} else {
		pix = pix;
	}
	return pix;
}

static void
fmt_u(char *dst, int cap, unsigned v)
{
	char rev[10];
	int n = 0;
	int i;
	int at = 0;

	if ((dst == 0) || (cap < 2)) {
		return;
	}
	if (v == 0u) {
		dst[0] = '0';
		dst[1] = '\0';
		return;
	}
	while ((v > 0u) && (n < 10)) {
		rev[n] = (char)('0' + (int)(v % 10u));
		n = n + 1;
		v = v / 10u;
	}
	for (i = n - 1; i >= 0; i = i - 1) {
		if (at < (cap - 1)) {
			dst[at] = rev[i];
			at = at + 1;
		} else {
			at = at;
		}
	}
	dst[at] = '\0';
}

static void
fill_rect(Window w, int x, int y, int rw, int rh, unsigned long color)
{
	if ((ui_gc == 0) || (rw < 1) || (rh < 1)) {
		return;
	}
	XSetForeground(dpy, ui_gc, color);
	XFillRectangle(dpy, w, ui_gc, x, y, (unsigned)rw, (unsigned)rh);
}

static void
draw_text(Window w, int x, int y, const char *s, unsigned long color)
{
	int n = 0;

	if ((ui_gc == 0) || (ui_font == 0) || (s == 0)) {
		return;
	}
	while (s[n] != '\0') {
		n = n + 1;
	}
	XSetForeground(dpy, ui_gc, color);
	XDrawString(dpy, w, ui_gc, x, y, s, n);
}

static void
paint_icon(void)
{
	int m;
	unsigned long bg;
	unsigned long fg;

	if ((icon == 0) || (ui_gc == 0)) {
		return;
	}
	bg = pixel_of(46, 46, 51);
	if (visible != 0) {
		fg = pixel_of(235, 235, 230);
	} else {
		fg = pixel_of(120, 120, 124);
	}
	fill_rect(icon, 0, 0, ICON_SIZE, ICON_SIZE, bg);
	m = 4;
	fill_rect(icon, m, m, ICON_SIZE - (m * 2), ICON_SIZE - (m * 2) - 3, pixel_of(20, 20, 24));
	fill_rect(icon, m + 3, m + 2, ICON_SIZE - (m * 2) - 6, ICON_SIZE - (m * 2) - 8, fg);
}

static void
draw_slider(int y, int value, unsigned long fill, const char *label)
{
	int fw;
	unsigned long track;
	unsigned long text;

	track = pixel_of(68, 68, 74);
	text = pixel_of(230, 230, 226);
	draw_text(dialog, SLIDER_X, y - 8, label, text);
	fill_rect(dialog, SLIDER_X, y, SLIDER_W, SLIDER_H, track);
	if (value < 0) {
		value = 0;
	} else if (value > 255) {
		value = 255;
	} else {
		value = value;
	}
	fw = (value * SLIDER_W) / 255;
	if (fw > 0) {
		fill_rect(dialog, SLIDER_X, y, fw, SLIDER_H, fill);
	} else {
		fw = 0;
	}
}

static void
paint_dialog(void)
{
	unsigned long bg;
	unsigned long text;
	unsigned long btn;
	char num[8];
	int pr;
	int pg;
	int pb;
	int cover;
	const char *hide_label;

	if ((dialog == 0) || (ui_gc == 0)) {
		return;
	}
	bg = pixel_of(32, 32, 36);
	text = pixel_of(230, 230, 226);
	btn = pixel_of(58, 58, 64);
	fill_rect(dialog, 0, 0, DLG_W, DLG_H, bg);
	fmt_u(num, (int)sizeof num, (unsigned)strength);
	draw_text(dialog, 220, LEVEL_Y - 8, num, text);
	draw_slider(LEVEL_Y, strength, pixel_of(210, 210, 206), "Level");
	draw_slider(RED_Y, red, pixel_of(196, 64, 64), "Red");
	draw_slider(GREEN_Y, green, pixel_of(64, 168, 96), "Green");
	draw_slider(BLUE_Y, blue, pixel_of(72, 112, 196), "Blue");
	cover = strength;
	if (cover < 0) {
		cover = 0;
	} else if (cover > 255) {
		cover = 255;
	} else {
		cover = cover;
	}
	pr = ((red * cover) + (255 * (255 - cover))) / 255;
	pg = ((green * cover) + (255 * (255 - cover))) / 255;
	pb = ((blue * cover) + (255 * (255 - cover))) / 255;
	fill_rect(dialog, PREVIEW_X, PREVIEW_Y, PREVIEW_S, PREVIEW_S, pixel_of(pr, pg, pb));
	fill_rect(dialog, HIDE_X, BTN_Y, BTN_W, BTN_H, btn);
	fill_rect(dialog, QUIT_X, BTN_Y, BTN_W, BTN_H, btn);
	if (visible != 0) {
		hide_label = "Hide";
	} else {
		hide_label = "Show";
	}
	draw_text(dialog, HIDE_X + 32, BTN_Y + 20, hide_label, text);
	draw_text(dialog, QUIT_X + 34, BTN_Y + 20, "Quit", text);
}

static void
ui_refresh(void)
{
	if (dialog_up != 0) {
		paint_dialog();
	} else {
		dialog_up = dialog_up;
	}
	if (icon != 0) {
		paint_icon();
	} else {
		icon = icon;
	}
}

static Window
tray_manager(void)
{
	char name[32];
	int n;
	unsigned scr_u;
	Atom sel;
	Window owner;

	if (scr < 0) {
		return 0;
	}
	n = copy_cap(name, (int)sizeof name, "_NET_SYSTEM_TRAY_S");
	if (n < 0) {
		return 0;
	}
	scr_u = (unsigned)scr;
	if (scr_u >= 100u) {
		return 0;
	}
	if (scr_u >= 10u) {
		if (n >= ((int)sizeof name - 2)) {
			return 0;
		}
		name[n] = (char)('0' + (int)(scr_u / 10u));
		n = n + 1;
		scr_u = scr_u % 10u;
	} else {
		scr_u = scr_u;
	}
	if (n >= ((int)sizeof name - 1)) {
		return 0;
	}
	name[n] = (char)('0' + (int)scr_u);
	n = n + 1;
	name[n] = '\0';
	sel = XInternAtom(dpy, name, False);
	owner = XGetSelectionOwner(dpy, sel);
	return owner;
}

static void
try_dock(void)
{
	Window manager;
	XClientMessageEvent ev;
	unsigned long info[2];

	if ((icon == 0) || (icon_embedded != 0)) {
		return;
	}
	manager = tray_manager();
	if (manager == 0) {
		dock_sent = 0;
		return;
	}
	if (dock_sent != 0) {
		return;
	}
	info[0] = 0u;
	info[1] = 1u;
	XChangeProperty(dpy, icon, atom_xembed_info, atom_xembed_info, 32,
		PropModeReplace, (unsigned char *)info, 2);
	memset(&ev, 0, sizeof ev);
	ev.type = ClientMessage;
	ev.window = manager;
	ev.message_type = atom_tray_op;
	ev.format = 32;
	ev.data.l[0] = CurrentTime;
	ev.data.l[1] = 0;
	ev.data.l[2] = (long)icon;
	ev.data.l[3] = 0;
	ev.data.l[4] = 0;
	XSendEvent(dpy, manager, False, NoEventMask, (XEvent *)&ev);
	dock_sent = 1;
}

static void
show_dialog(void)
{
	XClientMessageEvent ev;

	if (dialog == 0) {
		return;
	}
	XMapRaised(dpy, dialog);
	dialog_up = 1;
	memset(&ev, 0, sizeof ev);
	ev.type = ClientMessage;
	ev.window = dialog;
	ev.message_type = net_active;
	ev.format = 32;
	ev.data.l[0] = 1;
	ev.data.l[1] = CurrentTime;
	ev.data.l[2] = 0;
	ev.data.l[3] = 0;
	ev.data.l[4] = 0;
	XSendEvent(dpy, root, False,
		SubstructureRedirectMask | SubstructureNotifyMask, (XEvent *)&ev);
	paint_dialog();
}

static void
hide_dialog(void)
{
	if (dialog == 0) {
		return;
	}
	XUnmapWindow(dpy, dialog);
	dialog_up = 0;
}

static int
slider_value(int x)
{
	int v;

	v = x - SLIDER_X;
	if (v < 0) {
		v = 0;
	} else if (v >= SLIDER_W) {
		v = SLIDER_W - 1;
	} else {
		v = v;
	}
	if (SLIDER_W > 1) {
		v = (v * 255) / (SLIDER_W - 1);
	} else {
		v = 0;
	}
	if (v < 0) {
		v = 0;
	} else if (v > 255) {
		v = 255;
	} else {
		v = v;
	}
	return v;
}

static int
slider_at(int x, int y)
{
	int which = DRAG_NONE;

	if ((x < SLIDER_X) || (x >= (SLIDER_X + SLIDER_W))) {
		return DRAG_NONE;
	}
	if ((y >= LEVEL_Y) && (y < (LEVEL_Y + SLIDER_H))) {
		which = DRAG_LEVEL;
	} else if ((y >= RED_Y) && (y < (RED_Y + SLIDER_H))) {
		which = DRAG_RED;
	} else if ((y >= GREEN_Y) && (y < (GREEN_Y + SLIDER_H))) {
		which = DRAG_GREEN;
	} else if ((y >= BLUE_Y) && (y < (BLUE_Y + SLIDER_H))) {
		which = DRAG_BLUE;
	} else {
		which = DRAG_NONE;
	}
	return which;
}

static int
in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
	int ok = 0;

	if ((x >= rx) && (x < (rx + rw)) && (y >= ry) && (y < (ry + rh))) {
		ok = 1;
	} else {
		ok = 0;
	}
	return ok;
}

static void
ui_apply_drag(int which, int value)
{
	cmd c;

	c.kind = CMD_BAD;
	c.alpha = strength;
	c.red = red;
	c.green = green;
	c.blue = blue;
	c.visible = visible;
	if (which == DRAG_LEVEL) {
		c.kind = CMD_ALPHA;
		c.alpha = value;
	} else if (which == DRAG_RED) {
		c.kind = CMD_COLOR;
		c.red = value;
	} else if (which == DRAG_GREEN) {
		c.kind = CMD_COLOR;
		c.green = value;
	} else if (which == DRAG_BLUE) {
		c.kind = CMD_COLOR;
		c.blue = value;
	} else {
		which = DRAG_NONE;
	}
	if (c.kind != CMD_BAD) {
		apply_cmd(&c);
	} else {
		c.kind = CMD_BAD;
	}
}

static void
ui_button(int x, int y)
{
	int which;
	cmd c;
	int grab;

	which = slider_at(x, y);
	if (which != DRAG_NONE) {
		drag = which;
		grab = XGrabPointer(dpy, dialog, False,
			ButtonReleaseMask | PointerMotionMask,
			GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
		if (grab != GrabSuccess) {
			grab = 0;
		} else {
			grab = 1;
		}
		ui_apply_drag(which, slider_value(x));
	} else if (in_rect(x, y, HIDE_X, BTN_Y, BTN_W, BTN_H) != 0) {
		c.kind = CMD_BAD;
		c.alpha = 0;
		c.red = 0;
		c.green = 0;
		c.blue = 0;
		c.visible = 0;
		if (visible != 0) {
			c.kind = CMD_HIDE;
		} else {
			c.kind = CMD_SHOW;
		}
		apply_cmd(&c);
	} else if (in_rect(x, y, QUIT_X, BTN_Y, BTN_W, BTN_H) != 0) {
		c.kind = CMD_QUIT;
		c.alpha = 0;
		c.red = 0;
		c.green = 0;
		c.blue = 0;
		c.visible = 0;
		apply_cmd(&c);
	} else {
		which = DRAG_NONE;
	}
}

static int
ui_handle(XEvent *ev)
{
	int used = 0;

	if ((icon != 0) && (ev->xany.window == icon)) {
		used = 1;
		if (ev->type == Expose) {
			if (ev->xexpose.count == 0) {
				paint_icon();
			} else {
				used = 1;
			}
		} else if (ev->type == ButtonPress) {
			if (ev->xbutton.button == Button1) {
				if (dialog_up != 0) {
					hide_dialog();
				} else {
					show_dialog();
				}
			} else if (ev->xbutton.button == Button3) {
				cmd c;
				c.alpha = 0;
				c.red = 0;
				c.green = 0;
				c.blue = 0;
				c.visible = 0;
				if (visible != 0) {
					c.kind = CMD_HIDE;
				} else {
					c.kind = CMD_SHOW;
				}
				apply_cmd(&c);
			} else {
				used = 1;
			}
		} else if (ev->type == ReparentNotify) {
			if (ev->xreparent.parent != root) {
				icon_embedded = 1;
				paint_icon();
			} else {
				icon_embedded = 0;
				dock_sent = 0;
			}
		} else if ((ev->type == ClientMessage) && (ev->xclient.message_type == atom_xembed)) {
			if (ev->xclient.data.l[1] == 0) {
				icon_embedded = 1;
				paint_icon();
			} else {
				used = 1;
			}
		} else if (ev->type == DestroyNotify) {
			icon = 0;
			icon_embedded = 0;
			dock_sent = 0;
		} else {
			used = 1;
		}
	} else if ((dialog != 0) && (ev->xany.window == dialog)) {
		used = 1;
		if (ev->type == Expose) {
			if (ev->xexpose.count == 0) {
				paint_dialog();
			} else {
				used = 1;
			}
		} else if (ev->type == ButtonPress) {
			if (ev->xbutton.button == Button1) {
				ui_button(ev->xbutton.x, ev->xbutton.y);
			} else {
				used = 1;
			}
		} else if (ev->type == ButtonRelease) {
			if (drag != DRAG_NONE) {
				XUngrabPointer(dpy, CurrentTime);
				drag = DRAG_NONE;
			} else {
				drag = DRAG_NONE;
			}
		} else if (ev->type == MotionNotify) {
			if (drag != DRAG_NONE) {
				ui_apply_drag(drag, slider_value(ev->xmotion.x));
			} else {
				drag = DRAG_NONE;
			}
		} else if (ev->type == UnmapNotify) {
			dialog_up = 0;
		} else if ((ev->type == ClientMessage) &&
			(ev->xclient.message_type == atom_wm_protocols)) {
			if ((Atom)ev->xclient.data.l[0] == atom_wm_delete) {
				if (icon_embedded == 0) {
					quit_flag = 1;
				} else {
					hide_dialog();
				}
			} else {
				used = 1;
			}
		} else {
			used = 1;
		}
	} else {
		used = 0;
	}
	return used;
}

static void
ui_tick(void)
{
	static int waits;
	static int opened;

	if ((no_tray != 0) || (icon == 0)) {
		return;
	}
	if (icon_embedded == 0) {
		try_dock();
		waits = waits + 1;
		if (waits == 6) {
			dock_sent = 0;
		} else {
			waits = waits;
		}
		if ((waits >= 3) && (opened == 0)) {
			opened = 1;
			show_dialog();
		} else {
			opened = opened;
		}
	} else {
		waits = 0;
	}
}

static int
ui_init(void)
{
	XSetWindowAttributes attr;
	XGCValues gv;
	XSizeHints hints;
	XClassHint class_hint;
	Atom dialog_type;
	unsigned long mask;
	int ok = 1;

	atom_tray_op = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
	atom_xembed = XInternAtom(dpy, "_XEMBED", False);
	atom_xembed_info = XInternAtom(dpy, "_XEMBED_INFO", False);
	atom_wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	atom_wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
	memset(&attr, 0, sizeof attr);
	attr.background_pixel = BlackPixel(dpy, scr);
	attr.border_pixel = BlackPixel(dpy, scr);
	attr.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
		StructureNotifyMask;
	icon = XCreateWindow(dpy, root, 0, 0, ICON_SIZE, ICON_SIZE, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		CWBackPixel | CWBorderPixel | CWEventMask, &attr);
	attr.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
		PointerMotionMask | StructureNotifyMask;
	dialog = XCreateWindow(dpy, root, 80, 80, DLG_W, DLG_H, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		CWBackPixel | CWBorderPixel | CWEventMask, &attr);
	if ((icon == 0) || (dialog == 0)) {
		say("litedim: cannot create the settings window\n");
		ok = 0;
	} else {
		ui_font = XLoadQueryFont(dpy, "fixed");
		if (ui_font == 0) {
			ui_font = XLoadQueryFont(dpy, "6x13");
		} else {
			ui_font = ui_font;
		}
		memset(&gv, 0, sizeof gv);
		gv.graphics_exposures = False;
		if (ui_font != 0) {
			gv.font = ui_font->fid;
			mask = GCFont | GCGraphicsExposures;
		} else {
			mask = GCGraphicsExposures;
		}
		ui_gc = XCreateGC(dpy, dialog, mask, &gv);
		if (ui_gc == 0) {
			say("litedim: cannot create the settings graphics\n");
			ok = 0;
		} else {
			memset(&hints, 0, sizeof hints);
			hints.flags = PSize | PMinSize | PMaxSize;
			hints.width = ICON_SIZE;
			hints.height = ICON_SIZE;
			hints.min_width = ICON_SIZE;
			hints.min_height = ICON_SIZE;
			hints.max_width = ICON_SIZE;
			hints.max_height = ICON_SIZE;
			XSetWMNormalHints(dpy, icon, &hints);
			hints.width = DLG_W;
			hints.height = DLG_H;
			hints.min_width = DLG_W;
			hints.min_height = DLG_H;
			hints.max_width = DLG_W;
			hints.max_height = DLG_H;
			XSetWMNormalHints(dpy, dialog, &hints);
			class_hint.res_name = "litedim";
			class_hint.res_class = "Litedim";
			XSetClassHint(dpy, dialog, &class_hint);
			XStoreName(dpy, dialog, "litedim");
			XStoreName(dpy, icon, "litedim");
			XSetWMProtocols(dpy, dialog, &atom_wm_delete, 1);
			dialog_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
			XChangeProperty(dpy, dialog, net_wm_window_type, XA_ATOM, 32,
				PropModeReplace, (unsigned char *)&dialog_type, 1);
			try_dock();
		}
	}
	return ok;
}

static void
announce(void)
{
	say("litedim: ");
	if (visible != 0) {
		say("alpha ");
	} else {
		say("hidden, alpha ");
	}
	say_u((unsigned)strength);
	say(" color ");
	say_hex2(red);
	say_hex2(green);
	say_hex2(blue);
	say("\n");
}

static void
run(void)
{
	int xfd = ConnectionNumber(dpy);

	if (visible != 0) {
		show_sheet();
	} else {
		visible = 0;
	}
	announce();
	for (;;) {
		XEvent ev;
		struct pollfd fds[2];
		int pr;

		while (XPending(dpy) != 0) {
			XNextEvent(dpy, &ev);
			handle(&ev);
		}
		if (quit_flag != 0) {
			break;
		}
		if (need_place != 0) {
			need_place = 0;
			place();
		} else {
			XFlush(dpy);
			fds[0].fd = xfd;
			fds[0].events = POLLIN;
			fds[1].fd = listen_fd;
			fds[1].events = POLLIN;
			if ((no_tray == 0) && (icon_embedded == 0)) {
				pr = poll(fds, 2, 500);
			} else {
				pr = poll(fds, 2, -1);
			}
			if (pr < 0) {
				if (errno == EINTR) {
					pr = 0;
				} else {
					say("litedim: poll failed\n");
					break;
				}
			} else if (pr == 0) {
				ui_tick();
			} else {
				if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
					break;
				}
				if ((fds[1].revents & POLLIN) != 0) {
					serve_once();
				} else {
					pr = pr;
				}
			}
		}
	}
}

int
main(int argc, char **argv)
{
	int i;
	int evb;
	int errb;
	int have_alpha = 0;
	int have_color = 0;
	int cli_alpha = 0;
	int cli_red = 0;
	int cli_green = 0;
	int cli_blue = 0;
	int xfd;

	for (i = 1; i < argc; i = i + 1) {
		if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
			usage(STDOUT_FILENO);
			return 0;
		} else if ((strcmp(argv[i], "-a") == 0) || (strcmp(argv[i], "--alpha") == 0)) {
			if ((i + 1) >= argc) {
				usage(STDERR_FILENO);
				return 2;
			}
			i = i + 1;
			if (parse_u8(argv[i], (int)strlen(argv[i]), &cli_alpha) == 0) {
				say("litedim: alpha must be 0..255\n");
				return 2;
			}
			have_alpha = 1;
		} else if (strcmp(argv[i], "--color") == 0) {
			if ((i + 1) >= argc) {
				usage(STDERR_FILENO);
				return 2;
			}
			i = i + 1;
			if (parse_color(argv[i], (int)strlen(argv[i]), &cli_red, &cli_green, &cli_blue) == 0) {
				say("litedim: color must be RRGGBB\n");
				return 2;
			}
			have_color = 1;
		} else if (strcmp(argv[i], "--no-tray") == 0) {
			no_tray = 1;
		} else {
			say("litedim: unknown argument ");
			say(argv[i]);
			say("\n");
			usage(STDERR_FILENO);
			return 2;
		}
	}
	if (build_paths() == 0) {
		say("litedim: cannot build the control path\n");
		return 1;
	}
	if (ensure_dir() == 0) {
		return 1;
	}
	load_config();
	if (have_alpha != 0) {
		if (cli_alpha == 0) {
			visible = 0;
		} else {
			strength = cli_alpha;
			visible = 1;
		}
	} else {
		have_alpha = 0;
	}
	if (have_color != 0) {
		red = cli_red;
		green = cli_green;
		blue = cli_blue;
	} else {
		have_color = 0;
	}
	if (daemon_alive() != 0) {
		say("litedim: already running\n");
		return 0;
	}
	if (listen_bind() == 0) {
		cleanup_listen();
		return 1;
	}
	dpy = XOpenDisplay(0);
	if (dpy == 0) {
		say("litedim: cannot open the display\n");
		cleanup_listen();
		return 1;
	}
	xfd = ConnectionNumber(dpy);
	if (set_cloexec(xfd) == 0) {
		say("litedim: cannot mark the display connection close-on-exec\n");
		XCloseDisplay(dpy);
		cleanup_listen();
		return 1;
	}
	scr = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);
	XSetErrorHandler(on_xerror);
	if (compositor_present() == 0) {
		say("litedim: no compositor is running; refusing to map an opaque sheet\n");
		XCloseDisplay(dpy);
		cleanup_listen();
		return 1;
	}
	if (XRenderQueryExtension(dpy, &evb, &errb) == 0) {
		say("litedim: Render extension is missing\n");
		XCloseDisplay(dpy);
		cleanup_listen();
		return 1;
	}
	if (XFixesQueryExtension(dpy, &evb, &errb) == 0) {
		say("litedim: XFixes extension is missing\n");
		XCloseDisplay(dpy);
		cleanup_listen();
		return 1;
	}
	if (XShapeQueryExtension(dpy, &evb, &errb) == 0) {
		say("litedim: Shape extension is missing\n");
		XCloseDisplay(dpy);
		cleanup_listen();
		return 1;
	}
	net_active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	net_wm_window_type_desktop = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
	if (root_size() == 0) {
		XCloseDisplay(dpy);
		cleanup_listen();
		return 1;
	}
	if (create_sheet() == 0) {
		XCloseDisplay(dpy);
		cleanup_listen();
		return 1;
	}
	XSelectInput(dpy, root,
		PropertyChangeMask | StructureNotifyMask | SubstructureNotifyMask);
	if (save_config() == 0) {
		say("litedim: cannot save the settings\n");
	} else {
		visible = visible;
	}
	if (no_tray == 0) {
		if (ui_init() == 0) {
			say("litedim: continuing without the tray\n");
			no_tray = 1;
		} else {
			no_tray = 0;
		}
	} else {
		no_tray = 1;
	}
	run();
	XUnmapWindow(dpy, sheet);
	XDestroyWindow(dpy, sheet);
	XCloseDisplay(dpy);
	cleanup_listen();
	return 0;
}
