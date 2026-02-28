#include "../src/std.h"
#include "../src/word.h"
#include "../src/hiscoa-common.h"
#include "../src/hiscoa-compress.h"

#include <stdio.h>
#include <string.h>

static unsigned errors;

static void check(bool cond, const char *name)
{
	if (! cond) {
		fprintf(stderr, "FAIL: %s\n", name);
		++errors;
	}
}

static void test_lo_hi(void)
{
	check(LO(0x0000) == 0x00, "LO(0x0000)");
	check(HI(0x0000) == 0x00, "HI(0x0000)");
	check(LO(0x1234) == 0x34, "LO(0x1234)");
	check(HI(0x1234) == 0x12, "HI(0x1234)");
	check(LO(0x00FF) == 0xFF, "LO(0x00FF)");
	check(HI(0x00FF) == 0x00, "HI(0x00FF)");
	check(LO(0xFF00) == 0x00, "LO(0xFF00)");
	check(HI(0xFF00) == 0xFF, "HI(0xFF00)");
	check(LO(0xFFFF) == 0xFF, "LO(0xFFFF)");
	check(HI(0xFFFF) == 0xFF, "HI(0xFFFF)");
}

static void test_word(void)
{
	check(WORD(0x00, 0x00) == 0x0000, "WORD(0,0)");
	check(WORD(0x34, 0x12) == 0x1234, "WORD(0x34,0x12)");
	check(WORD(0xFF, 0x00) == 0x00FF, "WORD(0xFF,0x00)");
	check(WORD(0x00, 0xFF) == 0xFF00, "WORD(0x00,0xFF)");
	check(WORD(0xFF, 0xFF) == 0xFFFF, "WORD(0xFF,0xFF)");

	/* Roundtrip: WORD(LO(x), HI(x)) == x */
	{
		unsigned i;
		for (i = 0; i <= 0xFFFF; i += 0x0101) {
			uint16_t x = (uint16_t) i;
			check(WORD(LO(x), HI(x)) == x, "WORD/LO/HI roundtrip");
		}
	}
}

static void test_bcd(void)
{
	/* Valid BCD: 1234 decimal */
	check(BCD(0x34, 0x12) == 1234, "BCD(0x34,0x12)=1234");
	check(BCD(0x00, 0x00) == 0, "BCD(0x00,0x00)=0");
	check(BCD(0x99, 0x99) == 9999, "BCD(0x99,0x99)=9999");
	check(BCD(0x01, 0x00) == 1, "BCD(0x01,0x00)=1");
	check(BCD(0x10, 0x00) == 10, "BCD(0x10,0x00)=10");
	check(BCD(0x56, 0x78) == 7856, "BCD(0x56,0x78)=7856");

	/* Invalid BCD digits (>9): falls back to WORD() */
	check(BCD(0xAB, 0x12) == WORD(0xAB, 0x12), "BCD invalid lo falls back to WORD");
	check(BCD(0x12, 0xAB) == WORD(0x12, 0xAB), "BCD invalid hi falls back to WORD");
	check(BCD(0xFF, 0xFF) == WORD(0xFF, 0xFF), "BCD all-F falls back to WORD");
}

static void test_hiscoa_format_params(void)
{
	struct hiscoa_params p = {
		.origin_3 = 1,
		.origin_5 = 4,
		.origin_0 = 0,
		.origin_2 = -12,
		.origin_4 = 340,
	};
	uint8_t buf[16];
	size_t n;

	n = hiscoa_format_params(buf, sizeof(buf), &p);
	check(n == 8, "format_params returns 8");
	check(buf[0] == (uint8_t) 1, "params[0] = origin_3");
	check(buf[1] == (uint8_t) 4, "params[1] = origin_5");
	check(buf[4] == (uint8_t) 0, "params[4] = origin_0");
	check(buf[5] == (uint8_t)(int8_t) -12, "params[5] = origin_2");
	check(WORD(buf[6], buf[7]) == (uint16_t) 340, "params[6:7] = origin_4");

	/* Buffer too small */
	check(hiscoa_format_params(buf, 7, &p) == 0, "format_params too small returns 0");

	/* Negative origin_4 roundtrip */
	p.origin_4 = -100;
	n = hiscoa_format_params(buf, sizeof(buf), &p);
	check(n == 8, "format_params negative origin_4");
	check((int16_t) WORD(buf[6], buf[7]) == -100, "negative origin_4 roundtrip");
}

static void test_compress_empty(void)
{
	struct hiscoa_params p = {
		.origin_3 = 1,
		.origin_5 = 4,
		.origin_0 = 0,
		.origin_2 = -12,
		.origin_4 = 340,
	};
	uint8_t out[256];
	size_t n;

	/* Zero lines: should produce just the end marker + padding */
	n = hiscoa_compress_band(out, sizeof(out), NULL, 80, 0,
			HISCOA_EOB_NORMAL, &p);
	check(n > 0, "compress 0 lines produces output");
	check(n % 4 == 0, "compress output is 32-bit aligned");
}

int main(void)
{
	test_lo_hi();
	test_word();
	test_bcd();
	test_hiscoa_format_params();
	test_compress_empty();

	if (errors)
		fprintf(stderr, "FAILED: %u errors\n", errors);
	else
		fprintf(stderr, "PASSED: all tests OK\n");
	return errors ? 1 : 0;
}
