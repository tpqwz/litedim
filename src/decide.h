#ifndef LITEDIM_DECIDE_H
#define LITEDIM_DECIDE_H

#include <stdint.h>

enum { DECIDE_MAX_CHILDREN = 4096 };
enum { DECIDE_LINE_MAX = 63 };

typedef enum {
	PLACE_NONE = 0,
	PLACE_LOWER = 1,
	PLACE_BELOW = 2,
	PLACE_ABOVE = 3
} place_kind;

typedef struct {
	place_kind kind;
	int sibling;
} place_act;

typedef enum {
	CMD_BAD = 0,
	CMD_ALPHA = 1,
	CMD_COLOR = 2,
	CMD_SHOW = 3,
	CMD_HIDE = 4,
	CMD_QUIT = 5,
	CMD_STATUS = 6,
	CMD_VISIBLE = 7
} cmd_kind;

typedef struct {
	cmd_kind kind;
	int alpha;
	int red;
	int green;
	int blue;
	int visible;
} cmd;

uint16_t coverage_u16(int strength);
void premultiply(int red, int green, int blue, int strength,
	uint16_t *cr, uint16_t *cg, uint16_t *cb, uint16_t *ca);
place_act decide_place(int n, int sheet_at, int frame_at, int desk_at, int have_frame);
int place_sibling_ok(const place_act *act, int n);
int parse_u8(const char *s, int len, int *out);
int parse_color(const char *s, int len, int *red, int *green, int *blue);
cmd parse_line(const char *s, int len);
int format_state(char *dst, int cap, int alpha, int red, int green, int blue, int visible);

#endif
