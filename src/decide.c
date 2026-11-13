#include "decide.h"

static int
put_str(char *dst, int cap, int at, const char *s)
{
	int i = 0;

	if (at < 0) {
		return -1;
	}
	while (s[i] != '\0') {
		if ((at + i) >= (cap - 1)) {
			return -1;
		}
		dst[at + i] = s[i];
		i = i + 1;
	}
	dst[at + i] = '\0';
	return at + i;
}

static int
put_u(char *dst, int cap, int at, unsigned v)
{
	char tmp[10];
	int n = 0;
	int i;

	if (at < 0) {
		return -1;
	}
	if (v == 0u) {
		tmp[0] = '0';
		n = 1;
	} else {
		while ((v > 0u) && (n < 10)) {
			tmp[n] = (char)('0' + (int)(v % 10u));
			n = n + 1;
			v = v / 10u;
		}
	}
	if ((at + n) >= cap) {
		return -1;
	}
	for (i = 0; i < n; i = i + 1) {
		dst[at + i] = tmp[n - 1 - i];
	}
	dst[at + n] = '\0';
	return at + n;
}

static int
put_hex2(char *dst, int cap, int at, int v)
{
	static const char hex[] = "0123456789abcdef";

	if ((at < 0) || ((at + 2) >= cap) || (v < 0) || (v > 255)) {
		return -1;
	}
	dst[at] = hex[(v >> 4) & 15];
	dst[at + 1] = hex[v & 15];
	dst[at + 2] = '\0';
	return at + 2;
}

uint16_t
coverage_u16(int strength)
{
	int s = strength;
	uint32_t v;

	if ((s < 0) || (s > 255)) {
		s = 0;
	} else {
		s = strength;
	}
	v = ((uint32_t)s * 65535u + 127u) / 255u;
	if (v > 65535u) {
		v = 65535u;
	} else {
		v = v;
	}
	return (uint16_t)v;
}

void
premultiply(int red, int green, int blue, int strength,
	uint16_t *cr, uint16_t *cg, uint16_t *cb, uint16_t *ca)
{
	uint16_t a;
	uint32_t ar;
	uint32_t ag;
	uint32_t ab;

	if ((cr == 0) || (cg == 0) || (cb == 0) || (ca == 0)) {
		return;
	}
	a = coverage_u16(strength);
	if ((red < 0) || (red > 255)) {
		red = 0;
	} else {
		red = red;
	}
	if ((green < 0) || (green > 255)) {
		green = 0;
	} else {
		green = green;
	}
	if ((blue < 0) || (blue > 255)) {
		blue = 0;
	} else {
		blue = blue;
	}
	ar = ((uint32_t)red * 257u * (uint32_t)a) / 65535u;
	ag = ((uint32_t)green * 257u * (uint32_t)a) / 65535u;
	ab = ((uint32_t)blue * 257u * (uint32_t)a) / 65535u;
	if (ar > 65535u) {
		ar = 65535u;
	} else {
		ar = ar;
	}
	if (ag > 65535u) {
		ag = 65535u;
	} else {
		ag = ag;
	}
	if (ab > 65535u) {
		ab = 65535u;
	} else {
		ab = ab;
	}
	*ca = a;
	*cr = (uint16_t)ar;
	*cg = (uint16_t)ag;
	*cb = (uint16_t)ab;
}

place_act
decide_place(int n, int sheet_at, int frame_at, int desk_at, int have_frame)
{
	place_act act;

	act.kind = PLACE_NONE;
	act.sibling = -1;
	if ((n < 0) || (n > DECIDE_MAX_CHILDREN)) {
		act.kind = PLACE_NONE;
	} else if ((have_frame != 0) && (frame_at >= 0) && (frame_at < n)) {
		if ((sheet_at >= 0) && (sheet_at < n) && ((sheet_at + 1) == frame_at)) {
			act.kind = PLACE_NONE;
		} else {
			act.kind = PLACE_BELOW;
			act.sibling = frame_at;
		}
	} else if ((desk_at >= 0) && (desk_at < n)) {
		if ((sheet_at >= 0) && (sheet_at < n) && (sheet_at == (desk_at + 1))) {
			act.kind = PLACE_NONE;
		} else if ((desk_at + 1) < n) {
			act.kind = PLACE_BELOW;
			act.sibling = desk_at + 1;
		} else {
			act.kind = PLACE_ABOVE;
			act.sibling = desk_at;
		}
	} else {
		if ((n > 0) && (sheet_at == 0)) {
			act.kind = PLACE_NONE;
		} else {
			act.kind = PLACE_LOWER;
		}
	}
	if ((act.kind == PLACE_BELOW) || (act.kind == PLACE_ABOVE)) {
		if ((act.sibling < 0) || (act.sibling >= n)) {
			act.kind = PLACE_NONE;
			act.sibling = -1;
		} else {
			act.sibling = act.sibling;
		}
	} else {
		act.sibling = -1;
	}
	return act;
}

int
place_sibling_ok(const place_act *act, int n)
{
	int ok = 1;

	if (act == 0) {
		ok = 0;
	} else if ((act->kind == PLACE_BELOW) || (act->kind == PLACE_ABOVE)) {
		if ((n < 0) || (act->sibling < 0) || (act->sibling >= n)) {
			ok = 0;
		} else {
			ok = 1;
		}
	} else if ((act->kind == PLACE_NONE) || (act->kind == PLACE_LOWER)) {
		ok = 1;
	} else {
		ok = 0;
	}
	return ok;
}

int
parse_u8(const char *s, int len, int *out)
{
	int i = 0;
	int v = 0;
	int digits = 0;
	int ok = 1;

	if ((s == 0) || (out == 0) || (len < 1) || (len > DECIDE_LINE_MAX)) {
		return 0;
	}
	while ((ok == 1) && (i < len)) {
		if ((s[i] >= '0') && (s[i] <= '9')) {
			int dig = (int)(s[i] - '0');
			if (v > ((255 - dig) / 10)) {
				ok = 0;
			} else {
				v = (v * 10) + dig;
				digits = digits + 1;
				i = i + 1;
			}
		} else {
			ok = 0;
		}
	}
	if (digits < 1) {
		ok = 0;
	} else {
		ok = ok;
	}
	if (ok == 1) {
		*out = v;
	} else {
		ok = 0;
	}
	return ok;
}

static int
nibble(char c, int *out)
{
	int ok = 1;

	if ((c >= '0') && (c <= '9')) {
		*out = (int)(c - '0');
	} else if ((c >= 'a') && (c <= 'f')) {
		*out = (int)(c - 'a') + 10;
	} else if ((c >= 'A') && (c <= 'F')) {
		*out = (int)(c - 'A') + 10;
	} else {
		ok = 0;
	}
	return ok;
}

int
parse_color(const char *s, int len, int *red, int *green, int *blue)
{
	int h[6];
	int i;
	int ok = 1;

	if ((s == 0) || (red == 0) || (green == 0) || (blue == 0) || (len != 6)) {
		return 0;
	}
	for (i = 0; i < 6; i = i + 1) {
		if (nibble(s[i], &h[i]) == 0) {
			ok = 0;
		} else {
			ok = ok;
		}
	}
	if (ok == 1) {
		*red = (h[0] << 4) + h[1];
		*green = (h[2] << 4) + h[3];
		*blue = (h[4] << 4) + h[5];
	} else {
		ok = 0;
	}
	return ok;
}

static int
starts(const char *s, int len, const char *pfx)
{
	int i = 0;
	int ok = 1;

	while ((pfx[i] != '\0') && (ok == 1)) {
		if ((i >= len) || (s[i] != pfx[i])) {
			ok = 0;
		} else {
			i = i + 1;
		}
	}
	return ok;
}

static int
tail_ok(const char *s, int len, int i)
{
	int ok = 1;

	while ((i < len) && (ok == 1)) {
		if ((s[i] == ' ') || (s[i] == '\t')) {
			i = i + 1;
		} else {
			ok = 0;
		}
	}
	return ok;
}

cmd
parse_line(const char *s, int len)
{
	cmd c;
	int i;

	c.kind = CMD_BAD;
	c.alpha = 0;
	c.red = 0;
	c.green = 0;
	c.blue = 0;
	c.visible = 0;
	if ((s == 0) || (len < 0) || (len > DECIDE_LINE_MAX)) {
		return c;
	}
	while ((len > 0) && ((s[len - 1] == '\n') || (s[len - 1] == '\r'))) {
		len = len - 1;
	}
	if ((starts(s, len, "show") == 1) && (len == 4)) {
		c.kind = CMD_SHOW;
	} else if ((starts(s, len, "hide") == 1) && (len == 4)) {
		c.kind = CMD_HIDE;
	} else if ((starts(s, len, "quit") == 1) && (len == 4)) {
		c.kind = CMD_QUIT;
	} else if ((starts(s, len, "status") == 1) && (len == 6)) {
		c.kind = CMD_STATUS;
	} else if ((starts(s, len, "prev-alpha ") == 1) && (len > 11)) {
		if ((parse_u8(s + 11, len - 11, &c.alpha) == 1) && (tail_ok(s, len, len) == 1)) {
			c.kind = CMD_PREV_ALPHA;
		} else {
			c.kind = CMD_BAD;
		}
	} else if ((starts(s, len, "prev-color ") == 1) && (len >= 17)) {
		if (parse_color(s + 11, 6, &c.red, &c.green, &c.blue) == 1) {
			if (tail_ok(s, len, 17) == 1) {
				c.kind = CMD_PREV_COLOR;
			} else {
				c.kind = CMD_BAD;
			}
		} else {
			c.kind = CMD_BAD;
		}
	} else if ((starts(s, len, "fg-alpha ") == 1) && (len > 9)) {
		if ((parse_u8(s + 9, len - 9, &c.alpha) == 1) && (tail_ok(s, len, len) == 1)) {
			c.kind = CMD_FG_ALPHA;
		} else {
			c.kind = CMD_BAD;
		}
	} else if ((starts(s, len, "fg-color ") == 1) && (len >= 15)) {
		if (parse_color(s + 9, 6, &c.red, &c.green, &c.blue) == 1) {
			if (tail_ok(s, len, 15) == 1) {
				c.kind = CMD_FG_COLOR;
			} else {
				c.kind = CMD_BAD;
			}
		} else {
			c.kind = CMD_BAD;
		}
	} else if ((starts(s, len, "alpha ") == 1) && (len > 6)) {
		if ((parse_u8(s + 6, len - 6, &c.alpha) == 1) && (tail_ok(s, len, len) == 1)) {
			c.kind = CMD_ALPHA;
		} else {
			c.kind = CMD_BAD;
		}
	} else if ((starts(s, len, "color ") == 1) && (len >= 12)) {
		if (parse_color(s + 6, 6, &c.red, &c.green, &c.blue) == 1) {
			if (tail_ok(s, len, 12) == 1) {
				c.kind = CMD_COLOR;
			} else {
				c.kind = CMD_BAD;
			}
		} else {
			c.kind = CMD_BAD;
		}
	} else if ((starts(s, len, "visible ") == 1) && (len > 8)) {
		if (parse_u8(s + 8, len - 8, &i) == 1) {
			if ((i == 0) || (i == 1)) {
				c.kind = CMD_VISIBLE;
				c.visible = i;
			} else {
				c.kind = CMD_BAD;
			}
		} else {
			c.kind = CMD_BAD;
		}
	} else {
		c.kind = CMD_BAD;
	}
	return c;
}

int
format_state(char *dst, int cap, int alpha, int red, int green, int blue,
	int fg_alpha, int fg_red, int fg_green, int fg_blue,
	int prev_alpha, int prev_red, int prev_green, int prev_blue, int visible)
{
	int at = 0;
	unsigned vis = 0u;

	if ((dst == 0) || (cap < 112)) {
		return -1;
	}
	if ((alpha < 0) || (alpha > 255) || (red < 0) || (red > 255)) {
		return -1;
	}
	if ((green < 0) || (green > 255) || (blue < 0) || (blue > 255)) {
		return -1;
	}
	if ((fg_alpha < 0) || (fg_alpha > 255) || (fg_red < 0) || (fg_red > 255)) {
		return -1;
	}
	if ((fg_green < 0) || (fg_green > 255) || (fg_blue < 0) || (fg_blue > 255)) {
		return -1;
	}
	if ((prev_alpha < 0) || (prev_alpha > 255) || (prev_red < 0) || (prev_red > 255)) {
		return -1;
	}
	if ((prev_green < 0) || (prev_green > 255) || (prev_blue < 0) || (prev_blue > 255)) {
		return -1;
	}
	if (visible != 0) {
		vis = 1u;
	} else {
		vis = 0u;
	}
	at = put_str(dst, cap, at, "alpha ");
	at = put_u(dst, cap, at, (unsigned)alpha);
	at = put_str(dst, cap, at, "\ncolor ");
	at = put_hex2(dst, cap, at, red);
	at = put_hex2(dst, cap, at, green);
	at = put_hex2(dst, cap, at, blue);
	at = put_str(dst, cap, at, "\nfg-alpha ");
	at = put_u(dst, cap, at, (unsigned)fg_alpha);
	at = put_str(dst, cap, at, "\nfg-color ");
	at = put_hex2(dst, cap, at, fg_red);
	at = put_hex2(dst, cap, at, fg_green);
	at = put_hex2(dst, cap, at, fg_blue);
	at = put_str(dst, cap, at, "\nprev-alpha ");
	at = put_u(dst, cap, at, (unsigned)prev_alpha);
	at = put_str(dst, cap, at, "\nprev-color ");
	at = put_hex2(dst, cap, at, prev_red);
	at = put_hex2(dst, cap, at, prev_green);
	at = put_hex2(dst, cap, at, prev_blue);
	at = put_str(dst, cap, at, "\nvisible ");
	at = put_u(dst, cap, at, vis);
	at = put_str(dst, cap, at, "\n");
	if ((at < 0) || (at >= cap)) {
		return -1;
	}
	return at;
}
