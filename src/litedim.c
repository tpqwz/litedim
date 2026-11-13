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

static const char version_string[] = "0.1.0-beta.1";

static Display *dpy;
static int scr;
static Window root;
static Window sheet;
static Window front;
static Window dialog;
static Picture pict;
static Picture front_pict;
static Picture prev_pict;
static Window prev_shade;
static Window prev_frame;
static int sw;
static int sh;
static int strength = DEFAULT_ALPHA;
static int red;
static int green;
static int blue;
static int fg_strength;
static int fg_red;
static int fg_green;
static int fg_blue;
static int prev_follow = 1;
static int prev_strength;
static int prev_red;
static int prev_green;
static int prev_blue;
static int front_w;
static int front_h;
static int prev_w;
static int prev_h;
static int visible = 1;
static int no_tray;
static Window active_frame;
static Window kept_frame;
static Window dialog_frame;
static int dialog_up;
static int save_hold;
static int need_place;
static int quit_flag;
static int listen_fd = -1;
static int bound;
static char dir_path[96];
static char sock_path[108];
static char cfg_path[512];
static char legacy_cfg[108];
static int dlg_x;
static int dlg_y;
static int have_dlg_pos;
static int dlg_center;
static int dlg_center_tries;

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
		"usage: litedim [--alpha N] [--color RRGGBB]\n"
		"               [--fg-alpha N] [--fg-color RRGGBB]\n"
		"               [--prev-alpha N] [--prev-color RRGGBB] [--no-tray]\n"
		"       litedim --version\n"
		"\n"
		"Dim everything stacked under the focused window.\n"
		"N is the coverage, 0 through 255. The background default is 96.\n"
		"RRGGBB tints that shade. The default is black, 000000.\n"
		"--alpha 0 leaves the shades unmapped and keeps the saved level.\n"
		"--fg-alpha and --fg-color shade the focused window itself.\n"
		"The foreground default is 0, so that window stays bright.\n"
		"--prev-alpha shades the window that was focused just before it.\n"
		"Until you set it, that window matches the background.\n"
		"0 makes that window bright. A higher level is its own shade.\n"
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

static void
print_version(int fd)
{
	ssize_t w;

	w = write(fd, "litedim ", 8);
	if (w < 0) {
		w = 0;
	} else {
		w = w;
	}
	w = write(fd, version_string, sizeof version_string - 1u);
	if (w < 0) {
		w = 0;
	} else {
		w = w;
	}
	w = write(fd, "\n", 1);
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
path_append(char *dst, int cap, const char *suffix)
{
	int n = 0;
	int i = 0;

	if ((dst == 0) || (suffix == 0) || (cap < 2)) {
		return 0;
	}
	while (dst[n] != '\0') {
		n = n + 1;
		if (n >= cap) {
			return 0;
		}
	}
	if ((n > 0) && (dst[n - 1] != '/')) {
		if (n >= (cap - 1)) {
			return 0;
		}
		dst[n] = '/';
		n = n + 1;
	} else {
		n = n;
	}
	while (suffix[i] != '\0') {
		if (n >= (cap - 1)) {
			return 0;
		}
		dst[n] = suffix[i];
		n = n + 1;
		i = i + 1;
	}
	dst[n] = '\0';
	return 1;
}

static int
ensure_owned_dir(const char *path, mode_t mode, int force_mode)
{
	struct stat st;

	if (mkdir(path, mode) != 0) {
		if (errno != EEXIST) {
			return 0;
		}
	}
	if (lstat(path, &st) != 0) {
		return 0;
	}
	if ((S_ISDIR(st.st_mode) == 0) || (st.st_uid != getuid())) {
		return 0;
	}
	if (force_mode != 0) {
		if (chmod(path, mode) != 0) {
			return 0;
		}
	} else {
		force_mode = 0;
	}
	return 1;
}

static int
passwd_home(char *dst, int cap)
{
	char buf[512];
	int fd;
	int fill = 0;
	unsigned want;
	int found = 0;

	want = (unsigned)getuid();
	fd = open("/etc/passwd", O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return 0;
	}
	while (found == 0) {
		ssize_t n;
		int i = 0;
		int line = 0;

		n = read(fd, buf + fill, (size_t)((int)sizeof buf - fill));
		if (n < 0) {
			if (errno == EINTR) {
				n = 0;
			} else {
				break;
			}
		} else if (n == 0) {
			if ((fill > 0) && (fill < (int)sizeof buf)) {
				buf[fill] = '\n';
				fill = fill + 1;
			} else {
				break;
			}
		} else {
			fill = fill + (int)n;
		}
		while (i < fill) {
			if (buf[i] == '\n') {
				int field = 0;
				int start = line;
				int p = line;
				unsigned uid = 0u;
				int saw = 0;
				int home_at = -1;
				int home_n = 0;

				while (p <= i) {
					if ((p == i) || (buf[p] == ':')) {
						if (field == 2) {
							int k;

							uid = 0u;
							saw = 0;
							for (k = start; k < p; k = k + 1) {
								if ((buf[k] < '0') || (buf[k] > '9')) {
									saw = 0;
									k = p;
								} else {
									uid = (uid * 10u) + (unsigned)(buf[k] - '0');
									saw = 1;
								}
							}
						} else if (field == 5) {
							home_at = start;
							home_n = p - start;
						} else {
							field = field;
						}
						field = field + 1;
						start = p + 1;
					} else {
						p = p;
					}
					p = p + 1;
				}
				if ((saw != 0) && (uid == want) && (home_at >= 0) && (home_n > 0)) {
					int k;

					if (home_n >= cap) {
						found = 0;
					} else {
						for (k = 0; k < home_n; k = k + 1) {
							dst[k] = buf[home_at + k];
						}
						dst[home_n] = '\0';
						found = 1;
					}
				} else {
					saw = saw;
				}
				line = i + 1;
			} else {
				i = i;
			}
			i = i + 1;
		}
		if (found == 0) {
			int k;

			fill = fill - line;
			if (fill < 0) {
				fill = 0;
			} else if (line > 0) {
				for (k = 0; k < fill; k = k + 1) {
					buf[k] = buf[line + k];
				}
			} else if (fill >= (int)sizeof buf) {
				fill = 0;
			} else {
				fill = fill;
			}
		} else {
			found = 1;
		}
		if (n == 0) {
			break;
		}
	}
	if (close(fd) != 0) {
		found = found;
	} else {
		found = found;
	}
	return found;
}

/* Preferences live under the home directory. The socket stays in /tmp. */
static int
config_home(void)
{
	char home[256];
	char path[512];
	mode_t shared;

	if (passwd_home(home, (int)sizeof home) == 0) {
		return 0;
	}
	if (copy_cap(path, (int)sizeof path, home) < 0) {
		return 0;
	}
	if (path_append(path, (int)sizeof path, ".config") == 0) {
		return 0;
	}
	shared = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	if (ensure_owned_dir(path, shared, 0) == 0) {
		return 0;
	}
	if (path_append(path, (int)sizeof path, "litedim") == 0) {
		return 0;
	}
	if (ensure_owned_dir(path, S_IRWXU, 1) == 0) {
		return 0;
	}
	if (path_append(path, (int)sizeof path, "config") == 0) {
		return 0;
	}
	if (copy_cap(cfg_path, (int)sizeof cfg_path, path) < 0) {
		return 0;
	}
	return 1;
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
	if (join_path(legacy_cfg, (int)sizeof legacy_cfg, dir_path, "config") < 0) {
		return 0;
	}
	if (config_home() == 0) {
		if (copy_cap(cfg_path, (int)sizeof cfg_path, legacy_cfg) < 0) {
			return 0;
		}
		legacy_cfg[0] = '\0';
	} else {
		ok = 1;
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
	} else if (c->kind == CMD_FG_ALPHA) {
		fg_strength = c->alpha;
	} else if (c->kind == CMD_FG_COLOR) {
		fg_red = c->red;
		fg_green = c->green;
		fg_blue = c->blue;
	} else if (c->kind == CMD_PREV_ALPHA) {
		prev_strength = c->alpha;
	} else if (c->kind == CMD_PREV_COLOR) {
		prev_red = c->red;
		prev_green = c->green;
		prev_blue = c->blue;
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

static int
parse_coord_line(const char *s, int len, int *which, int *out)
{
	int i = 2;
	int sign = 1;
	int v = 0;
	int digits = 0;

	if ((s == 0) || (which == 0) || (out == 0) || (len < 3)) {
		return 0;
	}
	if ((s[0] == 'x') && (s[1] == ' ')) {
		*which = 0;
	} else if ((s[0] == 'y') && (s[1] == ' ')) {
		*which = 1;
	} else {
		return 0;
	}
	if (s[i] == '-') {
		sign = -1;
		i = i + 1;
	} else {
		sign = 1;
	}
	while (i < len) {
		int dig;

		if ((s[i] < '0') || (s[i] > '9')) {
			return 0;
		}
		dig = (int)(s[i] - '0');
		if (v > ((20000 - dig) / 10)) {
			return 0;
		}
		v = (v * 10) + dig;
		digits = digits + 1;
		i = i + 1;
	}
	if (digits < 1) {
		return 0;
	}
	*out = v * sign;
	return 1;
}

static int
parse_follow_line(const char *s, int len, int *out);

static int
load_config_file(const char *path)
{
	char buf[512];
	int fd;
	ssize_t n;
	int i = 0;
	int start = 0;
	int got_x = 0;
	int got_y = 0;
	int saw_prev = 0;
	int saw_follow = 0;
	int follow_bit = 1;

	if ((path == 0) || (path[0] == '\0')) {
		return 0;
	}
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		return 0;
	}
	n = read(fd, buf, sizeof buf);
	if (close(fd) != 0) {
		n = n;
	} else {
		n = n;
	}
	if (n <= 0) {
		return 0;
	}
	while (i <= (int)n) {
		if ((i == (int)n) || (buf[i] == '\n')) {
			int len = i - start;
			int which = 0;
			int coord = 0;

			if ((len > 0) && (len <= DECIDE_LINE_MAX)) {
				if ((len == 13) && (buf[start] == 'p') &&
				    (parse_follow_line(buf + start, len, &follow_bit) == 1)) {
					saw_follow = 1;
				} else if (parse_coord_line(buf + start, len, &which, &coord) == 1) {
					if (which == 0) {
						dlg_x = coord;
						got_x = 1;
					} else {
						dlg_y = coord;
						got_y = 1;
					}
				} else {
					cmd c = parse_line(buf + start, len);

					if ((c.kind == CMD_PREV_ALPHA) || (c.kind == CMD_PREV_COLOR)) {
						saw_prev = 1;
					} else {
						saw_prev = saw_prev;
					}
					apply_config_cmd(&c);
				}
			} else {
				len = len;
			}
			start = i + 1;
		} else {
			start = start;
		}
		i = i + 1;
	}
	if ((got_x != 0) && (got_y != 0)) {
		have_dlg_pos = 1;
		dlg_center = 0;
	} else {
		got_x = got_x;
	}
	if (saw_follow != 0) {
		prev_follow = follow_bit;
	} else if (saw_prev != 0) {
		prev_follow = 0;
	} else {
		prev_follow = 1;
	}
	return 1;
}

static int
parse_follow_line(const char *s, int len, int *out)
{
	const char prefix[] = "prev-follow ";
	int i;

	if ((s == 0) || (out == 0) || (len != 13)) {
		return 0;
	}
	for (i = 0; i < 12; i = i + 1) {
		if (s[i] != prefix[i]) {
			return 0;
		}
	}
	if (s[12] == '0') {
		*out = 0;
	} else if (s[12] == '1') {
		*out = 1;
	} else {
		return 0;
	}
	return 1;
}

static void
load_config(void)
{
	if (load_config_file(cfg_path) == 0) {
		if (load_config_file(legacy_cfg) == 0) {
			legacy_cfg[0] = legacy_cfg[0];
		}
	} else {
		cfg_path[0] = cfg_path[0];
	}
}

static int
append_coord(char *dst, int cap, int at, char key, int v)
{
	char rev[8];
	int n = 0;
	int i;
	int sign = 0;

	if ((dst == 0) || (at < 0) || (at >= (cap - 4))) {
		return -1;
	}
	dst[at] = key;
	at = at + 1;
	dst[at] = ' ';
	at = at + 1;
	if (v < 0) {
		sign = 1;
		v = -v;
	} else {
		sign = 0;
	}
	if (v > 20000) {
		v = 20000;
	} else {
		v = v;
	}
	if (v == 0) {
		rev[0] = '0';
		n = 1;
	} else {
		while ((v > 0) && (n < 8)) {
			rev[n] = (char)('0' + (v % 10));
			n = n + 1;
			v = v / 10;
		}
	}
	if (sign != 0) {
		if (at >= (cap - 1)) {
			return -1;
		}
		dst[at] = '-';
		at = at + 1;
	} else {
		sign = 0;
	}
	for (i = n - 1; i >= 0; i = i - 1) {
		if (at >= (cap - 1)) {
			return -1;
		}
		dst[at] = rev[i];
		at = at + 1;
	}
	if (at >= (cap - 1)) {
		return -1;
	}
	dst[at] = '\n';
	at = at + 1;
	dst[at] = '\0';
	return at;
}

static int
append_follow(char *dst, int cap, int at, int follow)
{
	const char *s;
	int i = 0;

	s = (follow != 0) ? "prev-follow 1\n" : "prev-follow 0\n";
	if ((dst == 0) || (at < 0)) {
		return -1;
	}
	while (s[i] != '\0') {
		if (at >= (cap - 1)) {
			return -1;
		}
		dst[at] = s[i];
		at = at + 1;
		i = i + 1;
	}
	dst[at] = '\0';
	return at;
}

static int
save_config(void)
{
	char body[160];
	int n;
	int fd;
	int off = 0;
	ssize_t w;
	int ok = 1;

	if (save_hold != 0) {
		return 1;
	}
	n = format_state(body, (int)sizeof body, strength, red, green, blue,
		fg_strength, fg_red, fg_green, fg_blue,
		prev_strength, prev_red, prev_green, prev_blue, visible);
	if (n < 0) {
		return 0;
	}
	if (have_dlg_pos != 0) {
		n = append_coord(body, (int)sizeof body, n, 'x', dlg_x);
		n = append_coord(body, (int)sizeof body, n, 'y', dlg_y);
		if (n < 0) {
			return 0;
		}
	} else {
		n = n;
	}
	n = append_follow(body, (int)sizeof body, n, prev_follow);
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
fill_picture(Picture picture, int r, int g, int b, int alpha, int w, int h)
{
	XRenderColor c;
	uint16_t cr;
	uint16_t cg;
	uint16_t cb;
	uint16_t ca;

	if ((picture == 0) || (w < 1) || (h < 1)) {
		return;
	}
	premultiply(r, g, b, alpha, &cr, &cg, &cb, &ca);
	c.red = cr;
	c.green = cg;
	c.blue = cb;
	c.alpha = ca;
	XRenderFillRectangle(dpy, PictOpSrc, picture, &c, 0, 0, (unsigned)w, (unsigned)h);
}

static void
paint(void)
{
	if (visible == 0) {
		return;
	}
	fill_picture(pict, red, green, blue, strength, sw, sh);
}

static void
paint_front(void)
{
	fill_picture(front_pict, fg_red, fg_green, fg_blue, fg_strength, front_w, front_h);
}

static void
click_through(Window w)
{
	/*
	 * An empty region is not the same as passing None. None would restore
	 * the default input shape, and the sheet would take clicks.
	 */
	XserverRegion empty;

	if (w == 0) {
		return;
	}
	empty = XFixesCreateRegion(dpy, 0, 0);
	XFixesSetWindowShapeRegion(dpy, w, ShapeInput, 0, 0, empty);
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
	if ((w == sheet) || (w == front) || (w == prev_shade)) {
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
stack_window(Window w, Window sibling, int mode)
{
	XWindowChanges ch;

	if ((w == 0) || (sibling == 0) || (w == sibling)) {
		return;
	}
	ch.sibling = sibling;
	ch.stack_mode = mode;
	XConfigureWindow(dpy, w, CWSibling | CWStackMode, &ch);
}

static void
unmap_front(void)
{
	if (front != 0) {
		XUnmapWindow(dpy, front);
	} else {
		front = front;
	}
}

static void
place_front(Window frame)
{
	XWindowAttributes attr;
	XWindowChanges ch;

	if ((front == 0) || (visible == 0) || (fg_strength <= 0) || (frame == 0)) {
		unmap_front();
		return;
	}
	if (XGetWindowAttributes(dpy, frame, &attr) == 0) {
		unmap_front();
		return;
	}
	if ((attr.map_state != IsViewable) || (attr.width < 1) || (attr.height < 1)) {
		unmap_front();
		return;
	}
	front_w = attr.width;
	front_h = attr.height;
	ch.x = attr.x;
	ch.y = attr.y;
	ch.width = attr.width;
	ch.height = attr.height;
	ch.sibling = frame;
	ch.stack_mode = Above;
	/* Map raises the window. Restack it in the same burst, just above the frame. */
	XMapWindow(dpy, front);
	XConfigureWindow(dpy, front,
		CWX | CWY | CWWidth | CWHeight | CWSibling | CWStackMode, &ch);
	paint_front();
}

static void
unmap_prev(void)
{
	if (prev_shade != 0) {
		XUnmapWindow(dpy, prev_shade);
	} else {
		prev_shade = prev_shade;
	}
}

static void
prev_level(int *alpha, int *redc, int *greenc, int *bluec)
{
	if (prev_follow != 0) {
		*alpha = strength;
		*redc = red;
		*greenc = green;
		*bluec = blue;
	} else {
		*alpha = prev_strength;
		*redc = prev_red;
		*greenc = prev_green;
		*bluec = prev_blue;
	}
}

static void
paint_prev(void)
{
	int alpha;
	int redc;
	int greenc;
	int bluec;

	prev_level(&alpha, &redc, &greenc, &bluec);
	fill_picture(prev_pict, redc, greenc, bluec, alpha, prev_w, prev_h);
}

static void
place_prev(Window current)
{
	XWindowAttributes attr;
	XWindowChanges ch;
	int alpha;
	int redc;
	int greenc;
	int bluec;

	prev_level(&alpha, &redc, &greenc, &bluec);
	if ((prev_shade == 0) || (visible == 0) || (alpha <= 0) ||
	    (prev_frame == 0) || (prev_frame == current)) {
		unmap_prev();
		return;
	}
	if (XGetWindowAttributes(dpy, prev_frame, &attr) == 0) {
		unmap_prev();
		return;
	}
	if ((attr.map_state != IsViewable) || (attr.width < 1) || (attr.height < 1)) {
		unmap_prev();
		return;
	}
	prev_w = attr.width;
	prev_h = attr.height;
	ch.x = attr.x;
	ch.y = attr.y;
	ch.width = attr.width;
	ch.height = attr.height;
	ch.sibling = prev_frame;
	ch.stack_mode = Above;
	XMapWindow(dpy, prev_shade);
	XConfigureWindow(dpy, prev_shade,
		CWX | CWY | CWWidth | CWHeight | CWSibling | CWStackMode, &ch);
	if ((current != 0) && (current != prev_frame)) {
		stack_window(prev_shade, current, Below);
	} else {
		current = current;
	}
	paint_prev();
}

static void
clear_bounding(Window w)
{
	if (w == 0) {
		return;
	}
	XFixesSetWindowShapeRegion(dpy, w, ShapeBounding, 0, 0, None);
}

static void
shape_except(Window w, int wx, int wy, int ww, int wh, int hx, int hy, int hw, int hh)
{
	XRectangle big;
	XRectangle hole;
	XserverRegion all;
	XserverRegion cut;
	XserverRegion out;

	if ((w == 0) || (ww < 1) || (wh < 1) || (hw < 1) || (hh < 1)) {
		return;
	}
	big.x = 0;
	big.y = 0;
	big.width = (unsigned short)ww;
	big.height = (unsigned short)wh;
	hole.x = (short)(hx - wx);
	hole.y = (short)(hy - wy);
	hole.width = (unsigned short)hw;
	hole.height = (unsigned short)hh;
	all = XFixesCreateRegion(dpy, &big, 1);
	cut = XFixesCreateRegion(dpy, &hole, 1);
	out = XFixesCreateRegion(dpy, 0, 0);
	if ((all != 0) && (cut != 0) && (out != 0)) {
		XFixesSubtractRegion(dpy, out, all, cut);
		XFixesSetWindowShapeRegion(dpy, w, ShapeBounding, 0, 0, out);
	} else {
		all = all;
	}
	if (all != 0) {
		XFixesDestroyRegion(dpy, all);
	} else {
		all = 0;
	}
	if (cut != 0) {
		XFixesDestroyRegion(dpy, cut);
	} else {
		cut = 0;
	}
	if (out != 0) {
		XFixesDestroyRegion(dpy, out);
	} else {
		out = 0;
	}
}

/* The shade is click-through, so it can sit above the settings window and
 * hide it. A hole in the bounding shape lets that window show through.
 */
static void
punch_one(Window w, int wx, int wy, int ww, int wh, int hx, int hy, int hw, int hh)
{
	if ((hw < 1) || (hh < 1)) {
		clear_bounding(w);
	} else {
		shape_except(w, wx, wy, ww, wh, hx, hy, hw, hh);
	}
}

static void
punch_shades(void)
{
	XWindowAttributes dialog_attr;
	XWindowAttributes prev_attr;
	XWindowAttributes shade_attr;
	int have_dialog = 0;
	int have_prev = 0;

	if ((dialog_up != 0) && (dialog_frame != 0) &&
	    (XGetWindowAttributes(dpy, dialog_frame, &dialog_attr) != 0) &&
	    (dialog_attr.width > 0) && (dialog_attr.height > 0)) {
		have_dialog = 1;
	} else {
		have_dialog = 0;
	}
	if ((prev_frame != 0) && (prev_frame != active_frame) &&
	    (XGetWindowAttributes(dpy, prev_frame, &prev_attr) != 0) &&
	    (prev_attr.map_state == IsViewable) && (prev_attr.width > 0) &&
	    (prev_attr.height > 0)) {
		have_prev = 1;
	} else {
		have_prev = 0;
	}
	if ((have_dialog == 0) && (have_prev == 0)) {
		clear_bounding(sheet);
		clear_bounding(front);
		clear_bounding(prev_shade);
		return;
	}
	if (have_dialog != 0) {
		shape_except(sheet, 0, 0, sw, sh, dialog_attr.x, dialog_attr.y,
			dialog_attr.width, dialog_attr.height);
	} else {
		clear_bounding(sheet);
	}
	if (have_prev != 0) {
		XRectangle big;
		XRectangle hole;
		XserverRegion all;
		XserverRegion cut;
		XserverRegion out;

		big.x = 0;
		big.y = 0;
		big.width = (unsigned short)sw;
		big.height = (unsigned short)sh;
		hole.x = (short)prev_attr.x;
		hole.y = (short)prev_attr.y;
		hole.width = (unsigned short)prev_attr.width;
		hole.height = (unsigned short)prev_attr.height;
		all = XFixesCreateRegion(dpy, &big, 1);
		cut = XFixesCreateRegion(dpy, &hole, 1);
		out = XFixesCreateRegion(dpy, 0, 0);
		if ((all != 0) && (cut != 0) && (out != 0)) {
			if (have_dialog != 0) {
				XRectangle dhole;
				XserverRegion dcut;

				dhole.x = (short)dialog_attr.x;
				dhole.y = (short)dialog_attr.y;
				dhole.width = (unsigned short)dialog_attr.width;
				dhole.height = (unsigned short)dialog_attr.height;
				dcut = XFixesCreateRegion(dpy, &dhole, 1);
				if (dcut != 0) {
					XFixesSubtractRegion(dpy, all, all, dcut);
					XFixesDestroyRegion(dpy, dcut);
				} else {
					dcut = 0;
				}
			} else {
				have_dialog = 0;
			}
			XFixesSubtractRegion(dpy, out, all, cut);
			XFixesSetWindowShapeRegion(dpy, sheet, ShapeBounding, 0, 0, out);
		} else {
			all = all;
		}
		if (all != 0) {
			XFixesDestroyRegion(dpy, all);
		} else {
			all = 0;
		}
		if (cut != 0) {
			XFixesDestroyRegion(dpy, cut);
		} else {
			cut = 0;
		}
		if (out != 0) {
			XFixesDestroyRegion(dpy, out);
		} else {
			out = 0;
		}
	} else {
		have_prev = 0;
	}
	if ((front != 0) && (XGetWindowAttributes(dpy, front, &shade_attr) != 0)) {
		if (have_dialog != 0) {
			punch_one(front, shade_attr.x, shade_attr.y, shade_attr.width,
				shade_attr.height, dialog_attr.x, dialog_attr.y,
				dialog_attr.width, dialog_attr.height);
		} else {
			clear_bounding(front);
		}
	} else {
		front = front;
	}
	if ((prev_shade != 0) && (XGetWindowAttributes(dpy, prev_shade, &shade_attr) != 0)) {
		if (have_dialog != 0) {
			punch_one(prev_shade, shade_attr.x, shade_attr.y, shade_attr.width,
				shade_attr.height, dialog_attr.x, dialog_attr.y,
				dialog_attr.width, dialog_attr.height);
		} else {
			clear_bounding(prev_shade);
		}
	} else {
		prev_shade = prev_shade;
	}
}

static void
reveal_dialog(void)
{
	Window frame;
	XWindowAttributes attr;

	if ((dialog_up == 0) || (dialog == 0)) {
		dialog_frame = 0;
		punch_shades();
		return;
	}
	frame = toplevel(dialog);
	dialog_frame = frame;
	if ((frame == 0) || (XGetWindowAttributes(dpy, frame, &attr) == 0)) {
		return;
	}
	if ((attr.width < 1) || (attr.height < 1)) {
		return;
	}
	if ((dlg_center != 0) && (frame != dialog)) {
		int tx;
		int ty;
		int dx;
		int dy;

		tx = (sw - attr.width) / 2;
		ty = (sh - attr.height) / 2;
		if (tx < 0) {
			tx = 0;
		} else {
			tx = tx;
		}
		if (ty < 0) {
			ty = 0;
		} else {
			ty = ty;
		}
		dx = attr.x - tx;
		dy = attr.y - ty;
		if (dx < 0) {
			dx = -dx;
		} else {
			dx = dx;
		}
		if (dy < 0) {
			dy = -dy;
		} else {
			dy = dy;
		}
		if ((dlg_center_tries < 1) && ((dx > 8) || (dy > 8))) {
			dlg_center_tries = 1;
			XMoveWindow(dpy, frame, tx, ty);
		} else {
			dlg_center = 0;
		}
	} else if (frame != dialog) {
		dlg_center = 0;
	} else {
		frame = frame;
	}
	if ((frame != dialog) && (dlg_center == 0) &&
	    ((have_dlg_pos == 0) || (attr.x != dlg_x) || (attr.y != dlg_y))) {
		dlg_x = attr.x;
		dlg_y = attr.y;
		have_dlg_pos = 1;
		if (save_config() == 0) {
			say("litedim: cannot save the settings\n");
		} else {
			have_dlg_pos = 1;
		}
	} else {
		frame = frame;
	}
	punch_shades();
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
	Window frame = 0;
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
		unmap_front();
		unmap_prev();
		XFree(kids);
		return;
	}
	sheet_at = index_of(kids, n, sheet);
	client = active_window();
	if ((dialog != 0) && (client == dialog) && (kept_frame != 0)) {
		frame = kept_frame;
	} else if ((client != 0) && (window_is_desktop(client) == 0)) {
		frame = toplevel(client);
		if (frame != 0) {
			if ((kept_frame != 0) && (frame != kept_frame)) {
				prev_frame = kept_frame;
			} else {
				frame = frame;
			}
			kept_frame = frame;
		} else {
			frame = 0;
		}
	} else {
		frame = 0;
	}
	if (frame != 0) {
		active_frame = frame;
		frame_at = index_of(kids, n, frame);
		if (frame_at >= 0) {
			have_frame = 1;
		} else {
			have_frame = 0;
			active_frame = 0;
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
		stack_window(sheet, kids[act.sibling], Below);
	} else if (act.kind == PLACE_ABOVE) {
		stack_window(sheet, kids[act.sibling], Above);
	} else if (act.kind == PLACE_LOWER) {
		XLowerWindow(dpy, sheet);
	} else {
		act.kind = PLACE_NONE;
	}
	if (have_frame == 0) {
		frame = 0;
	} else {
		frame = frame;
	}
	XFree(kids);
	place_front(frame);
	place_prev(frame);
	reveal_dialog();
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
	unmap_front();
	unmap_prev();
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
			click_through(sheet);
			paint();
		}
	}
	return ok;
}

static int
create_front(void)
{
	XSetWindowAttributes attr;
	XRenderPictFormat *fmt;
	Colormap cmap;
	Visual *argb;
	XClassHint class_hint;
	int ok = 1;

	argb = find_argb_visual();
	if (argb == 0) {
		say("litedim: no 32-bit ARGB visual for the focused window\n");
		return 0;
	}
	fmt = XRenderFindVisualFormat(dpy, argb);
	if (fmt == 0) {
		say("litedim: no Render format for the focused window\n");
		return 0;
	}
	cmap = XCreateColormap(dpy, root, argb, AllocNone);
	memset(&attr, 0, sizeof attr);
	attr.override_redirect = True;
	attr.colormap = cmap;
	attr.background_pixmap = None;
	attr.border_pixel = 0;
	attr.event_mask = ExposureMask;
	front_w = 1;
	front_h = 1;
	front = XCreateWindow(dpy, root, 0, 0, 1u, 1u, 0,
		32, InputOutput, argb,
		CWOverrideRedirect | CWColormap | CWBackPixmap | CWBorderPixel | CWEventMask,
		&attr);
	if (front == 0) {
		say("litedim: cannot create the foreground shade\n");
		ok = 0;
	} else {
		class_hint.res_name = "litedim-front";
		class_hint.res_class = "Litedim";
		XSetClassHint(dpy, front, &class_hint);
		XStoreName(dpy, front, "litedim-front");
		front_pict = XRenderCreatePicture(dpy, front, fmt, 0, 0);
		if (front_pict == 0) {
			say("litedim: cannot create the foreground picture\n");
			ok = 0;
		} else {
			click_through(front);
		}
	}
	return ok;
}

static int
create_prev(void)
{
	XSetWindowAttributes attr;
	XRenderPictFormat *fmt;
	Colormap cmap;
	Visual *argb;
	XClassHint class_hint;
	int ok = 1;

	argb = find_argb_visual();
	if (argb == 0) {
		say("litedim: no 32-bit ARGB visual for the previous window\n");
		return 0;
	}
	fmt = XRenderFindVisualFormat(dpy, argb);
	if (fmt == 0) {
		say("litedim: no Render format for the previous window\n");
		return 0;
	}
	cmap = XCreateColormap(dpy, root, argb, AllocNone);
	memset(&attr, 0, sizeof attr);
	attr.override_redirect = True;
	attr.colormap = cmap;
	attr.background_pixmap = None;
	attr.border_pixel = 0;
	attr.event_mask = ExposureMask;
	prev_w = 1;
	prev_h = 1;
	prev_shade = XCreateWindow(dpy, root, 0, 0, 1u, 1u, 0,
		32, InputOutput, argb,
		CWOverrideRedirect | CWColormap | CWBackPixmap | CWBorderPixel | CWEventMask,
		&attr);
	if (prev_shade == 0) {
		say("litedim: cannot create the previous-window shade\n");
		ok = 0;
	} else {
		class_hint.res_name = "litedim-prev";
		class_hint.res_class = "Litedim";
		XSetClassHint(dpy, prev_shade, &class_hint);
		XStoreName(dpy, prev_shade, "litedim-prev");
		prev_pict = XRenderCreatePicture(dpy, prev_shade, fmt, 0, 0);
		if (prev_pict == 0) {
			say("litedim: cannot create the previous-window picture\n");
			ok = 0;
		} else {
			click_through(prev_shade);
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
		} else if ((ev->xexpose.window == front) && (ev->xexpose.count == 0)) {
			paint_front();
		} else if ((ev->xexpose.window == prev_shade) && (ev->xexpose.count == 0)) {
			paint_prev();
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
		} else if (((active_frame != 0) && (c->window == active_frame)) ||
			((prev_frame != 0) && (c->window == prev_frame)) ||
			((dialog_frame != 0) && (c->window == dialog_frame))) {
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
	char body[160];
	int n;
	ssize_t w;
	int off = 0;

	n = format_state(body, (int)sizeof body, strength, red, green, blue,
		fg_strength, fg_red, fg_green, fg_blue,
		prev_strength, prev_red, prev_green, prev_blue, visible);
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
		int was = strength;

		strength = c->alpha;
		if (visible == 0) {
			was = was;
		} else if (strength == 0) {
			XUnmapWindow(dpy, sheet);
		} else if (was == 0) {
			XMapWindow(dpy, sheet);
			paint();
			place();
		} else {
			paint();
			if (prev_follow != 0) {
				paint_prev();
			} else {
				was = was;
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
			if (prev_follow != 0) {
				paint_prev();
			} else {
				visible = visible;
			}
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
	} else if (c->kind == CMD_FG_ALPHA) {
		int was = fg_strength;

		fg_strength = c->alpha;
		if (visible == 0) {
			was = was;
		} else if ((was == 0) || (fg_strength == 0)) {
			place();
		} else {
			paint_front();
		}
		if (save_config() == 0) {
			say("litedim: cannot save the settings\n");
		} else {
			fg_strength = fg_strength;
		}
	} else if (c->kind == CMD_FG_COLOR) {
		fg_red = c->red;
		fg_green = c->green;
		fg_blue = c->blue;
		if ((visible != 0) && (fg_strength > 0)) {
			paint_front();
		} else {
			fg_red = fg_red;
		}
		if (save_config() == 0) {
			say("litedim: cannot save the settings\n");
		} else {
			fg_red = fg_red;
		}
	} else if (c->kind == CMD_PREV_ALPHA) {
		int was = prev_strength;

		prev_follow = 0;
		prev_strength = c->alpha;
		if (visible == 0) {
			was = was;
		} else if ((was == 0) || (prev_strength == 0)) {
			place();
		} else {
			paint_prev();
			reveal_dialog();
		}
		if (save_config() == 0) {
			say("litedim: cannot save the settings\n");
		} else {
			prev_strength = prev_strength;
		}
	} else if (c->kind == CMD_PREV_COLOR) {
		prev_red = c->red;
		prev_green = c->green;
		prev_blue = c->blue;
		if ((visible != 0) && (prev_strength > 0)) {
			paint_prev();
		} else {
			prev_red = prev_red;
		}
		if (save_config() == 0) {
			say("litedim: cannot save the settings\n");
		} else {
			prev_red = prev_red;
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
enum { DLG_H = 536 };
enum { SLIDER_X = 16 };
enum { SLIDER_W = 248 };
enum { SLIDER_H = 16 };
enum { BG_LEVEL_Y = 36 };
enum { BG_RED_Y = 72 };
enum { BG_GREEN_Y = 108 };
enum { BG_BLUE_Y = 144 };
enum { PREV_LEVEL_Y = 196 };
enum { PREV_RED_Y = 232 };
enum { PREV_GREEN_Y = 268 };
enum { PREV_BLUE_Y = 304 };
enum { FG_LEVEL_Y = 356 };
enum { FG_RED_Y = 392 };
enum { FG_GREEN_Y = 428 };
enum { FG_BLUE_Y = 464 };
enum { PREVIEW_X = 12 };
enum { PREVIEW_Y = 496 };
enum { PREVIEW_S = 22 };
enum { HIDE_X = 100 };
enum { QUIT_X = 190 };
enum { BTN_Y = 496 };
enum { BTN_W = 80 };
enum { BTN_H = 32 };
enum { DRAG_NONE = 0 };
enum { DRAG_LEVEL = 1 };
enum { DRAG_RED = 2 };
enum { DRAG_GREEN = 3 };
enum { DRAG_BLUE = 4 };
enum { DRAG_FG_LEVEL = 5 };
enum { DRAG_FG_RED = 6 };
enum { DRAG_FG_GREEN = 7 };
enum { DRAG_FG_BLUE = 8 };
enum { DRAG_PREV_LEVEL = 9 };
enum { DRAG_PREV_RED = 10 };
enum { DRAG_PREV_GREEN = 11 };
enum { DRAG_PREV_BLUE = 12 };

static Window icon;
static GC ui_gc;
static XFontStruct *ui_font;
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
fill_disk(Window w, int x, int y, int rw, int rh, unsigned long color)
{
	/* XFillArc angles are in sixty-fourths of a degree. */
	enum { ARC_FULL = 360 * 64 };

	if ((ui_gc == 0) || (rw < 1) || (rh < 1)) {
		return;
	}
	XSetForeground(dpy, ui_gc, color);
	XFillArc(dpy, w, ui_gc, x, y, (unsigned)rw, (unsigned)rh, 0, ARC_FULL);
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
	unsigned long bg;
	unsigned long glass;
	unsigned long shine;
	unsigned long filament;
	unsigned long metal;

	if ((icon == 0) || (ui_gc == 0)) {
		return;
	}
	bg = pixel_of(46, 46, 51);
	if (visible != 0) {
		glass = pixel_of(255, 196, 72);
		shine = pixel_of(255, 236, 190);
		filament = pixel_of(196, 108, 32);
		metal = pixel_of(96, 82, 66);
	} else {
		glass = pixel_of(108, 110, 118);
		shine = pixel_of(150, 152, 160);
		filament = pixel_of(72, 74, 80);
		metal = pixel_of(64, 64, 70);
	}
	fill_rect(icon, 0, 0, ICON_SIZE, ICON_SIZE, bg);
	fill_disk(icon, 5, 1, 12, 12, glass);
	fill_disk(icon, 8, 3, 4, 3, shine);
	fill_rect(icon, 10, 6, 2, 4, filament);
	fill_rect(icon, 9, 11, 4, 3, metal);
	fill_rect(icon, 7, 14, 8, 5, metal);
	fill_rect(icon, 7, 15, 8, 1, bg);
	fill_rect(icon, 7, 17, 8, 1, bg);
	fill_rect(icon, 9, 19, 4, 2, metal);
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

static unsigned long
preview_pixel(int r, int g, int b, int cover)
{
	int pr;
	int pg;
	int pb;

	if (cover < 0) {
		cover = 0;
	} else if (cover > 255) {
		cover = 255;
	} else {
		cover = cover;
	}
	pr = ((r * cover) + (255 * (255 - cover))) / 255;
	pg = ((g * cover) + (255 * (255 - cover))) / 255;
	pb = ((b * cover) + (255 * (255 - cover))) / 255;
	return pixel_of(pr, pg, pb);
}

static void
paint_dialog(void)
{
	unsigned long bg;
	unsigned long text;
	unsigned long btn;
	char num[8];
	const char *hide_label;

	if ((dialog == 0) || (ui_gc == 0)) {
		return;
	}
	bg = pixel_of(32, 32, 36);
	text = pixel_of(230, 230, 226);
	btn = pixel_of(58, 58, 64);
	fill_rect(dialog, 0, 0, DLG_W, DLG_H, bg);
	draw_text(dialog, SLIDER_X, 14, "Background", text);
	fmt_u(num, (int)sizeof num, (unsigned)strength);
	draw_text(dialog, 220, BG_LEVEL_Y - 8, num, text);
	draw_slider(BG_LEVEL_Y, strength, pixel_of(210, 210, 206), "Level");
	draw_slider(BG_RED_Y, red, pixel_of(196, 64, 64), "Red");
	draw_slider(BG_GREEN_Y, green, pixel_of(64, 168, 96), "Green");
	draw_slider(BG_BLUE_Y, blue, pixel_of(72, 112, 196), "Blue");
	draw_text(dialog, SLIDER_X, 174, "Last window", text);
	if (prev_follow != 0) {
		fmt_u(num, (int)sizeof num, (unsigned)strength);
	} else {
		fmt_u(num, (int)sizeof num, (unsigned)prev_strength);
	}
	draw_text(dialog, 220, PREV_LEVEL_Y - 8, num, text);
	if (prev_follow != 0) {
		draw_slider(PREV_LEVEL_Y, strength, pixel_of(210, 210, 206), "Level");
		draw_slider(PREV_RED_Y, red, pixel_of(196, 64, 64), "Red");
		draw_slider(PREV_GREEN_Y, green, pixel_of(64, 168, 96), "Green");
		draw_slider(PREV_BLUE_Y, blue, pixel_of(72, 112, 196), "Blue");
	} else {
		draw_slider(PREV_LEVEL_Y, prev_strength, pixel_of(210, 210, 206), "Level");
		draw_slider(PREV_RED_Y, prev_red, pixel_of(196, 64, 64), "Red");
		draw_slider(PREV_GREEN_Y, prev_green, pixel_of(64, 168, 96), "Green");
		draw_slider(PREV_BLUE_Y, prev_blue, pixel_of(72, 112, 196), "Blue");
	}
	draw_text(dialog, SLIDER_X, 334, "Foreground", text);
	fmt_u(num, (int)sizeof num, (unsigned)fg_strength);
	draw_text(dialog, 220, FG_LEVEL_Y - 8, num, text);
	draw_slider(FG_LEVEL_Y, fg_strength, pixel_of(210, 210, 206), "Level");
	draw_slider(FG_RED_Y, fg_red, pixel_of(196, 64, 64), "Red");
	draw_slider(FG_GREEN_Y, fg_green, pixel_of(64, 168, 96), "Green");
	draw_slider(FG_BLUE_Y, fg_blue, pixel_of(72, 112, 196), "Blue");
	fill_rect(dialog, PREVIEW_X, PREVIEW_Y, PREVIEW_S, PREVIEW_S,
		preview_pixel(red, green, blue, strength));
	if (prev_follow != 0) {
		fill_rect(dialog, PREVIEW_X + 26, PREVIEW_Y, PREVIEW_S, PREVIEW_S,
			preview_pixel(red, green, blue, strength));
	} else {
		fill_rect(dialog, PREVIEW_X + 26, PREVIEW_Y, PREVIEW_S, PREVIEW_S,
			preview_pixel(prev_red, prev_green, prev_blue, prev_strength));
	}
	fill_rect(dialog, PREVIEW_X + 52, PREVIEW_Y, PREVIEW_S, PREVIEW_S,
		preview_pixel(fg_red, fg_green, fg_blue, fg_strength));
	fill_rect(dialog, HIDE_X, BTN_Y, BTN_W, BTN_H, btn);
	fill_rect(dialog, QUIT_X, BTN_Y, BTN_W, BTN_H, btn);
	if (visible != 0) {
		hide_label = "Hide";
	} else {
		hide_label = "Show";
	}
	draw_text(dialog, HIDE_X + 26, BTN_Y + 20, hide_label, text);
	draw_text(dialog, QUIT_X + 26, BTN_Y + 20, "Quit", text);
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
	if (dialog == 0) {
		return;
	}
	XMapRaised(dpy, dialog);
	dialog_up = 1;
	need_place = 1;
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
	reveal_dialog();
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
	if ((y >= BG_LEVEL_Y) && (y < (BG_LEVEL_Y + SLIDER_H))) {
		which = DRAG_LEVEL;
	} else if ((y >= BG_RED_Y) && (y < (BG_RED_Y + SLIDER_H))) {
		which = DRAG_RED;
	} else if ((y >= BG_GREEN_Y) && (y < (BG_GREEN_Y + SLIDER_H))) {
		which = DRAG_GREEN;
	} else if ((y >= BG_BLUE_Y) && (y < (BG_BLUE_Y + SLIDER_H))) {
		which = DRAG_BLUE;
	} else if ((y >= PREV_LEVEL_Y) && (y < (PREV_LEVEL_Y + SLIDER_H))) {
		which = DRAG_PREV_LEVEL;
	} else if ((y >= PREV_RED_Y) && (y < (PREV_RED_Y + SLIDER_H))) {
		which = DRAG_PREV_RED;
	} else if ((y >= PREV_GREEN_Y) && (y < (PREV_GREEN_Y + SLIDER_H))) {
		which = DRAG_PREV_GREEN;
	} else if ((y >= PREV_BLUE_Y) && (y < (PREV_BLUE_Y + SLIDER_H))) {
		which = DRAG_PREV_BLUE;
	} else if ((y >= FG_LEVEL_Y) && (y < (FG_LEVEL_Y + SLIDER_H))) {
		which = DRAG_FG_LEVEL;
	} else if ((y >= FG_RED_Y) && (y < (FG_RED_Y + SLIDER_H))) {
		which = DRAG_FG_RED;
	} else if ((y >= FG_GREEN_Y) && (y < (FG_GREEN_Y + SLIDER_H))) {
		which = DRAG_FG_GREEN;
	} else if ((y >= FG_BLUE_Y) && (y < (FG_BLUE_Y + SLIDER_H))) {
		which = DRAG_FG_BLUE;
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
	} else if (which == DRAG_PREV_LEVEL) {
		if (prev_follow != 0) {
			prev_strength = strength;
			prev_red = red;
			prev_green = green;
			prev_blue = blue;
			prev_follow = 0;
		} else {
			prev_follow = 0;
		}
		c.kind = CMD_PREV_ALPHA;
		c.alpha = value;
	} else if (which == DRAG_PREV_RED) {
		if (prev_follow != 0) {
			prev_strength = strength;
			prev_red = red;
			prev_green = green;
			prev_blue = blue;
			prev_follow = 0;
		} else {
			prev_follow = 0;
		}
		c.kind = CMD_PREV_COLOR;
		c.red = prev_red;
		c.green = prev_green;
		c.blue = prev_blue;
		c.red = value;
	} else if (which == DRAG_PREV_GREEN) {
		if (prev_follow != 0) {
			prev_strength = strength;
			prev_red = red;
			prev_green = green;
			prev_blue = blue;
			prev_follow = 0;
		} else {
			prev_follow = 0;
		}
		c.kind = CMD_PREV_COLOR;
		c.red = prev_red;
		c.green = prev_green;
		c.blue = prev_blue;
		c.green = value;
	} else if (which == DRAG_PREV_BLUE) {
		if (prev_follow != 0) {
			prev_strength = strength;
			prev_red = red;
			prev_green = green;
			prev_blue = blue;
			prev_follow = 0;
		} else {
			prev_follow = 0;
		}
		c.kind = CMD_PREV_COLOR;
		c.red = prev_red;
		c.green = prev_green;
		c.blue = prev_blue;
		c.blue = value;
	} else if (which == DRAG_FG_LEVEL) {
		c.kind = CMD_FG_ALPHA;
		c.alpha = value;
	} else if (which == DRAG_FG_RED) {
		c.kind = CMD_FG_COLOR;
		c.red = fg_red;
		c.green = fg_green;
		c.blue = fg_blue;
		c.red = value;
	} else if (which == DRAG_FG_GREEN) {
		c.kind = CMD_FG_COLOR;
		c.red = fg_red;
		c.green = fg_green;
		c.blue = fg_blue;
		c.green = value;
	} else if (which == DRAG_FG_BLUE) {
		c.kind = CMD_FG_COLOR;
		c.red = fg_red;
		c.green = fg_green;
		c.blue = fg_blue;
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
		save_hold = 1;
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
				save_hold = 0;
				if (save_config() == 0) {
					say("litedim: cannot save the settings\n");
				} else {
					save_hold = 0;
				}
			} else {
				drag = DRAG_NONE;
			}
		} else if (ev->type == MotionNotify) {
			if (drag != DRAG_NONE) {
				ui_apply_drag(drag, slider_value(ev->xmotion.x));
			} else {
				drag = DRAG_NONE;
			}
		} else if (ev->type == MapNotify) {
			dialog_up = 1;
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
	if (have_dlg_pos == 0) {
		dlg_x = (sw - DLG_W) / 2;
		dlg_y = (sh - DLG_H) / 2;
		if (dlg_x < 0) {
			dlg_x = 0;
		} else {
			dlg_x = dlg_x;
		}
		if (dlg_y < 0) {
			dlg_y = 0;
		} else {
			dlg_y = dlg_y;
		}
		dlg_center = 1;
		dlg_center_tries = 0;
	} else {
		dlg_center = 0;
	}
	dialog = XCreateWindow(dpy, root, dlg_x, dlg_y, DLG_W, DLG_H, 0,
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
			hints.flags = USPosition | PPosition | PSize | PMinSize | PMaxSize;
			hints.x = dlg_x;
			hints.y = dlg_y;
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
	say("litedim ");
	say(version_string);
	if (visible != 0) {
		say(": alpha ");
	} else {
		say(": hidden, alpha ");
	}
	say_u((unsigned)strength);
	say(" color ");
	say_hex2(red);
	say_hex2(green);
	say_hex2(blue);
	say(" front ");
	say_u((unsigned)fg_strength);
	say(" color ");
	say_hex2(fg_red);
	say_hex2(fg_green);
	say_hex2(fg_blue);
	say(" previous ");
	if (prev_follow != 0) {
		say_u((unsigned)strength);
	} else {
		say_u((unsigned)prev_strength);
	}
	say(" color ");
	say_hex2(prev_red);
	say_hex2(prev_green);
	say_hex2(prev_blue);
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
	int have_fg_alpha = 0;
	int have_fg_color = 0;
	int have_prev_alpha = 0;
	int have_prev_color = 0;
	int cli_alpha = 0;
	int cli_red = 0;
	int cli_green = 0;
	int cli_blue = 0;
	int cli_fg_alpha = 0;
	int cli_fg_red = 0;
	int cli_fg_green = 0;
	int cli_fg_blue = 0;
	int cli_prev_alpha = 0;
	int cli_prev_red = 0;
	int cli_prev_green = 0;
	int cli_prev_blue = 0;
	int xfd;

	for (i = 1; i < argc; i = i + 1) {
		if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
			usage(STDOUT_FILENO);
			return 0;
		} else if (strcmp(argv[i], "--version") == 0) {
			print_version(STDOUT_FILENO);
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
		} else if (strcmp(argv[i], "--fg-alpha") == 0) {
			if ((i + 1) >= argc) {
				usage(STDERR_FILENO);
				return 2;
			}
			i = i + 1;
			if (parse_u8(argv[i], (int)strlen(argv[i]), &cli_fg_alpha) == 0) {
				say("litedim: fg-alpha must be 0..255\n");
				return 2;
			}
			have_fg_alpha = 1;
		} else if (strcmp(argv[i], "--fg-color") == 0) {
			if ((i + 1) >= argc) {
				usage(STDERR_FILENO);
				return 2;
			}
			i = i + 1;
			if (parse_color(argv[i], (int)strlen(argv[i]),
				&cli_fg_red, &cli_fg_green, &cli_fg_blue) == 0) {
				say("litedim: fg-color must be RRGGBB\n");
				return 2;
			}
			have_fg_color = 1;
		} else if (strcmp(argv[i], "--prev-alpha") == 0) {
			if ((i + 1) >= argc) {
				usage(STDERR_FILENO);
				return 2;
			}
			i = i + 1;
			if (parse_u8(argv[i], (int)strlen(argv[i]), &cli_prev_alpha) == 0) {
				say("litedim: prev-alpha must be 0..255\n");
				return 2;
			}
			have_prev_alpha = 1;
		} else if (strcmp(argv[i], "--prev-color") == 0) {
			if ((i + 1) >= argc) {
				usage(STDERR_FILENO);
				return 2;
			}
			i = i + 1;
			if (parse_color(argv[i], (int)strlen(argv[i]),
				&cli_prev_red, &cli_prev_green, &cli_prev_blue) == 0) {
				say("litedim: prev-color must be RRGGBB\n");
				return 2;
			}
			have_prev_color = 1;
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
	if (have_fg_alpha != 0) {
		fg_strength = cli_fg_alpha;
	} else {
		have_fg_alpha = 0;
	}
	if (have_fg_color != 0) {
		fg_red = cli_fg_red;
		fg_green = cli_fg_green;
		fg_blue = cli_fg_blue;
	} else {
		have_fg_color = 0;
	}
	if (have_prev_alpha != 0) {
		prev_strength = cli_prev_alpha;
		prev_follow = 0;
	} else {
		have_prev_alpha = 0;
	}
	if (have_prev_color != 0) {
		prev_follow = 0;
		prev_red = cli_prev_red;
		prev_green = cli_prev_green;
		prev_blue = cli_prev_blue;
	} else {
		have_prev_color = 0;
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
	if (create_front() == 0) {
		say("litedim: continuing without a foreground shade\n");
		if (front != 0) {
			XDestroyWindow(dpy, front);
		} else {
			front = front;
		}
		front = 0;
	} else {
		front = front;
	}
	if (create_prev() == 0) {
		say("litedim: continuing without a previous-window shade\n");
		if (prev_shade != 0) {
			XDestroyWindow(dpy, prev_shade);
		} else {
			prev_shade = prev_shade;
		}
		prev_shade = 0;
	} else {
		prev_shade = prev_shade;
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
	unmap_front();
	unmap_prev();
	XDestroyWindow(dpy, sheet);
	if (front != 0) {
		XDestroyWindow(dpy, front);
	} else {
		front = front;
	}
	if (prev_shade != 0) {
		XDestroyWindow(dpy, prev_shade);
	} else {
		prev_shade = prev_shade;
	}
	XCloseDisplay(dpy);
	cleanup_listen();
	return 0;
}
