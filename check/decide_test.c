#include <assert.h>
#include <string.h>

#include "decide.h"

static void
test_coverage(void)
{
	uint16_t cr;
	uint16_t cg;
	uint16_t cb;
	uint16_t ca;
	uint16_t a128;

	assert(coverage_u16(0) == 0);
	assert(coverage_u16(255) == 65535);
	assert(coverage_u16(128) == (uint16_t)((128u * 65535u + 127u) / 255u));
	assert(coverage_u16(-1) == 0);
	assert(coverage_u16(256) == 0);
	a128 = coverage_u16(128);
	assert((65535u - (unsigned)a128) == 32639u);
	premultiply(0, 0, 0, 128, &cr, &cg, &cb, &ca);
	assert(cr == 0 && cg == 0 && cb == 0 && ca == a128);
	premultiply(255, 0, 0, 128, &cr, &cg, &cb, &ca);
	assert(ca == a128 && cr == a128 && cg == 0 && cb == 0);
	premultiply(1, 2, 3, 10, 0, &cg, &cb, &ca);
}

static void
expect_place(int n, int sheet, int frame, int desk, int have, place_kind kind, int sibling)
{
	place_act act = decide_place(n, sheet, frame, desk, have);

	assert(place_sibling_ok(&act, n) == 1);
	assert(act.kind == kind);
	if (kind == PLACE_BELOW || kind == PLACE_ABOVE) {
		assert(act.sibling == sibling);
	} else {
		assert(act.sibling == -1);
	}
}

static void
test_place(void)
{
	place_act act;

	expect_place(10, 4, 5, -1, 1, PLACE_NONE, -1);
	expect_place(10, 2, 5, -1, 1, PLACE_BELOW, 5);
	expect_place(10, -1, 0, -1, 1, PLACE_BELOW, 0);
	expect_place(4, 1, -1, 0, 0, PLACE_NONE, -1);
	expect_place(4, 3, -1, 0, 0, PLACE_BELOW, 1);
	expect_place(4, 0, -1, 3, 0, PLACE_ABOVE, 3);
	expect_place(4, 0, -1, -1, 0, PLACE_NONE, -1);
	expect_place(4, 2, -1, -1, 0, PLACE_LOWER, -1);
	expect_place(4097, 0, 1, 0, 1, PLACE_NONE, -1);
	act = decide_place(4, 2, 3, 0, 1);
	assert(place_sibling_ok(0, 4) == 0);
	assert(place_sibling_ok(&act, 4) == 1);
	act.kind = (place_kind)9;
	assert(place_sibling_ok(&act, 4) == 0);
}

static void
test_place_sweep(void)
{
	int n;
	int sheet;
	int frame;
	int desk;
	int have;

	for (n = 0; n <= 24; n = n + 1) {
		for (sheet = -1; sheet <= n; sheet = sheet + 1) {
			for (frame = -1; frame <= n; frame = frame + 1) {
				for (desk = -1; desk <= n; desk = desk + 1) {
					for (have = 0; have <= 1; have = have + 1) {
						place_act act = decide_place(n, sheet, frame, desk, have);
						assert(place_sibling_ok(&act, n) == 1);
					}
				}
			}
		}
	}
	expect_place(4096, 4095, 4095, 4095, 1, PLACE_BELOW, 4095);
	expect_place(4096, -1, -1, 4095, 0, PLACE_ABOVE, 4095);
}

static void
test_parse(void)
{
	cmd c;
	int v = 9;
	int r = 0;
	int g = 0;
	int b = 0;
	char buf[160];
	int n;
	int i = 0;
	int start = 0;
	int seen = 0;

	assert(parse_u8("0", 1, &v) == 1 && v == 0);
	assert(parse_u8("255", 3, &v) == 1 && v == 255);
	assert(parse_u8("256", 3, &v) == 0);
	assert(parse_u8("12a", 3, &v) == 0);
	assert(parse_u8(0, 1, &v) == 0);
	assert(parse_color("00ff80", 6, &r, &g, &b) == 1);
	assert(r == 0 && g == 255 && b == 128);
	assert(parse_color("00ff8", 5, &r, &g, &b) == 0);
	assert(parse_color("gg0000", 6, &r, &g, &b) == 0);

	c = parse_line("show", 4);
	assert(c.kind == CMD_SHOW);
	c = parse_line("hide\n", 5);
	assert(c.kind == CMD_HIDE);
	c = parse_line("quit", 4);
	assert(c.kind == CMD_QUIT);
	c = parse_line("status", 6);
	assert(c.kind == CMD_STATUS);
	c = parse_line("alpha 128", 9);
	assert(c.kind == CMD_ALPHA && c.alpha == 128);
	c = parse_line("alpha 256", 9);
	assert(c.kind == CMD_BAD);
	c = parse_line("color ff00aa", 12);
	assert(c.kind == CMD_COLOR && c.red == 255 && c.green == 0 && c.blue == 170);
	c = parse_line("fg-alpha 40", 11);
	assert(c.kind == CMD_FG_ALPHA && c.alpha == 40);
	c = parse_line("fg-alpha 256", 12);
	assert(c.kind == CMD_BAD);
	c = parse_line("fg-color 010203", 15);
	assert(c.kind == CMD_FG_COLOR && c.red == 1 && c.green == 2 && c.blue == 3);
	c = parse_line("prev-alpha 7", 12);
	assert(c.kind == CMD_PREV_ALPHA && c.alpha == 7);
	c = parse_line("prev-alpha 256", 14);
	assert(c.kind == CMD_BAD);
	c = parse_line("prev-color aabbcc", 17);
	assert(c.kind == CMD_PREV_COLOR && c.red == 170 && c.green == 187 && c.blue == 204);
	c = parse_line("visible 0", 9);
	assert(c.kind == CMD_VISIBLE && c.visible == 0);
	c = parse_line("visible 2", 9);
	assert(c.kind == CMD_BAD);
	c = parse_line("nope", 4);
	assert(c.kind == CMD_BAD);

	n = format_state(buf, 160, 96, 18, 52, 255, 40, 1, 2, 3, 7, 170, 187, 204, 0);
	assert(n > 0 && n < 160);
	assert(format_state(0, 160, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1) == -1);
	assert(strcmp(buf,
		"alpha 96\ncolor 1234ff\nfg-alpha 40\nfg-color 010203\nprev-alpha 7\nprev-color aabbcc\nvisible 0\n") == 0);
	while (i < n) {
		if (buf[i] == '\n') {
			c = parse_line(buf + start, i - start);
			if (seen == 0) {
				assert(c.kind == CMD_ALPHA && c.alpha == 96);
			} else if (seen == 1) {
				assert(c.kind == CMD_COLOR && c.red == 18 && c.green == 52 && c.blue == 255);
			} else if (seen == 2) {
				assert(c.kind == CMD_FG_ALPHA && c.alpha == 40);
			} else if (seen == 3) {
				assert(c.kind == CMD_FG_COLOR && c.red == 1 && c.green == 2 && c.blue == 3);
			} else if (seen == 4) {
				assert(c.kind == CMD_PREV_ALPHA && c.alpha == 7);
			} else if (seen == 5) {
				assert(c.kind == CMD_PREV_COLOR && c.red == 170 && c.green == 187 && c.blue == 204);
			} else {
				assert(seen == 6);
				assert(c.kind == CMD_VISIBLE && c.visible == 0);
			}
			seen = seen + 1;
			start = i + 1;
		}
		i = i + 1;
	}
	assert(seen == 7);
	assert(format_state(buf, 160, 96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1) == 90);
}

int
main(void)
{
	test_coverage();
	test_place();
	test_place_sweep();
	test_parse();
	return 0;
}
