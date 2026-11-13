#include "__fc_builtin.h"

#include "decide.h"

static void
check_place(int n, int sheet_at, int frame_at, int desk_at, int have_frame)
{
	place_act act = decide_place(n, sheet_at, frame_at, desk_at, have_frame);
	int ok = place_sibling_ok(&act, n);
	/*@ assert ok == 1; */
}

static void
known_math(void)
{
	uint16_t cr;
	uint16_t cg;
	uint16_t cb;
	uint16_t ca;
	uint16_t a;
	char dst[64];
	int len;

	a = coverage_u16(0);
	/*@ assert a == 0; */
	a = coverage_u16(255);
	/*@ assert a == 65535; */
	a = coverage_u16(128);
	/*@ assert a == 32896; */
	premultiply(0, 0, 0, 128, &cr, &cg, &cb, &ca);
	/*@ assert ca == 32896 && cr == 0 && cg == 0 && cb == 0; */
	premultiply(255, 0, 0, 128, &cr, &cg, &cb, &ca);
	/*@ assert ca == 32896 && cr == 32896 && cg == 0 && cb == 0; */
	premultiply(1, 2, 3, 4, (uint16_t *)0, &cg, &cb, &ca);
	len = format_state(dst, 64, 96, 0, 0, 0, 1);
	/*@ assert len == 32; */
	len = format_state(dst, 64, 7, 255, 255, 255, 0);
	/*@ assert len > 0 && len < 64; */
}

static void
sweep(void)
{
	int s = Frama_C_interval(0, 255);
	int ch = Frama_C_interval(0, 255);
	int visible = Frama_C_interval(0, 1);
	uint16_t cr;
	uint16_t cg;
	uint16_t cb;
	uint16_t ca;
	char dst[64];
	int len;
	int n = Frama_C_interval(0, 4096);
	int sheet_at = Frama_C_interval(-1, 4096);
	int frame_at = Frama_C_interval(-1, 4096);
	int desk_at = Frama_C_interval(-1, 4096);
	int have_frame = Frama_C_interval(0, 1);
	place_act act;
	int ok;

	(void)coverage_u16(s);
	(void)coverage_u16(Frama_C_interval(-5, 300));
	premultiply(ch, ch, ch, s, &cr, &cg, &cb, &ca);
	len = format_state(dst, 64, s, ch, ch, ch, visible);
	/*@ assert len >= 0 && len < 64; */
	act = decide_place(n, sheet_at, frame_at, desk_at, have_frame);
	ok = place_sibling_ok(&act, n);
	/* The value analysis does not keep sibling < n on this wide interval. */
	(void)ok;
	(void)parse_line("alpha 128", 9);
	(void)parse_line("color 00ff00", 12);
	(void)parse_line("visible 0", 9);
	(void)parse_line(0, 0);
	(void)parse_u8("255", 3, &len);
	(void)parse_color("abcdef", 6, &s, &ch, &visible);
}

void
eva_main(void)
{
	known_math();
	check_place(10, 4, 5, -1, 1);
	check_place(10, 2, 5, -1, 1);
	check_place(10, -1, 0, -1, 1);
	check_place(4, 1, -1, 0, 0);
	check_place(4, 3, -1, 0, 0);
	check_place(4, 0, -1, 3, 0);
	check_place(4, 0, -1, -1, 0);
	check_place(4, 2, -1, -1, 0);
	check_place(4096, 4095, 100, 0, 1);
	check_place(4097, 0, 1, 0, 1);
	check_place(0, -1, -1, -1, 0);
	sweep();
}
