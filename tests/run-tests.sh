#!/bin/sh
# Hi-SCoA compression roundtrip integration tests.
# Generates various PBM images and verifies compress/decompress roundtrip.

set -e

TESTDIR=$(mktemp -d)
trap 'rm -rf "$TESTDIR"' EXIT

HISCOA=./test-hiscoa
WORD=./test-word
PASS=0
FAIL=0

run_test() {
	name="$1"
	file="$2"
	if "$HISCOA" "$file" 2>/dev/null; then
		PASS=$((PASS + 1))
	else
		echo "FAIL: $name"
		FAIL=$((FAIL + 1))
	fi
}

# Generate a PBM file: gen_pbm <file> <width> <height> <fill>
# fill: zero, ones, random, checker, stripe
gen_pbm() {
	file="$1"
	w="$2"
	h="$3"
	fill="$4"
	bytes=$((w / 8 * h))
	printf 'P4\n#\n%u %u\n' "$w" "$h" > "$file"
	case "$fill" in
		zero)
			dd if=/dev/zero bs="$bytes" count=1 2>/dev/null >> "$file"
			;;
		ones)
			dd if=/dev/zero bs="$bytes" count=1 2>/dev/null | tr '\0' '\377' >> "$file"
			;;
		random)
			dd if=/dev/urandom bs="$bytes" count=1 2>/dev/null >> "$file"
			;;
		checker)
			# 0xAA = 10101010, 0x55 = 01010101 alternating lines
			line=$((w / 8))
			i=0
			while [ "$i" -lt "$h" ]; do
				if [ $((i % 2)) -eq 0 ]; then
					dd if=/dev/zero bs="$line" count=1 2>/dev/null | tr '\0' '\252' >> "$file"
				else
					dd if=/dev/zero bs="$line" count=1 2>/dev/null | tr '\0' '\125' >> "$file"
				fi
				i=$((i + 1))
			done
			;;
		stripe)
			# alternating FF/00 bytes per line
			line=$((w / 8))
			i=0
			while [ "$i" -lt "$h" ]; do
				j=0
				while [ "$j" -lt "$line" ]; do
					if [ $((j % 2)) -eq 0 ]; then
						printf '\377' >> "$file"
					else
						printf '\0' >> "$file"
					fi
					j=$((j + 1))
				done
				i=$((i + 1))
			done
			;;
	esac
}

# --- Run unit tests ---
echo "=== Unit tests ==="
if "$WORD" 2>/dev/null; then
	PASS=$((PASS + 1))
else
	echo "FAIL: test-word"
	FAIL=$((FAIL + 1))
fi

# --- Roundtrip tests ---
echo "=== Hi-SCoA roundtrip tests ==="

# Minimal: 8 pixels wide (1 byte/line), 1 line
gen_pbm "$TESTDIR/min.pbm" 8 1 zero
run_test "8x1 zero" "$TESTDIR/min.pbm"

# Single band boundary: 70 lines (default band_size)
gen_pbm "$TESTDIR/1band.pbm" 640 70 random
run_test "640x70 random (1 band)" "$TESTDIR/1band.pbm"

# Two bands: 140 lines
gen_pbm "$TESTDIR/2band.pbm" 640 140 random
run_test "640x140 random (2 bands)" "$TESTDIR/2band.pbm"

# Band boundary +1: 71 lines (split across 2 bands)
gen_pbm "$TESTDIR/boundary.pbm" 640 71 random
run_test "640x71 random (band boundary)" "$TESTDIR/boundary.pbm"

# All-zero (white) page
gen_pbm "$TESTDIR/white.pbm" 4960 700 zero
run_test "4960x700 all-zero (white)" "$TESTDIR/white.pbm"

# All-one (black) page
gen_pbm "$TESTDIR/black.pbm" 4960 700 ones
run_test "4960x700 all-one (black)" "$TESTDIR/black.pbm"

# Checkerboard pattern (good for match testing)
gen_pbm "$TESTDIR/check.pbm" 640 140 checker
run_test "640x140 checker" "$TESTDIR/check.pbm"

# Stripe pattern (byte-level repetition)
gen_pbm "$TESTDIR/stripe.pbm" 640 140 stripe
run_test "640x140 stripe" "$TESTDIR/stripe.pbm"

# Random large (full A4-ish page)
gen_pbm "$TESTDIR/large.pbm" 4960 7016 random
run_test "4960x7016 random (full page)" "$TESTDIR/large.pbm"

# Narrow image (8px = 1 byte line, many lines)
gen_pbm "$TESTDIR/narrow.pbm" 8 500 random
run_test "8x500 random (narrow)" "$TESTDIR/narrow.pbm"

# Single line, wide
gen_pbm "$TESTDIR/wide1.pbm" 4960 1 random
run_test "4960x1 random (wide single line)" "$TESTDIR/wide1.pbm"

# --- Summary ---
echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="

[ "$FAIL" -eq 0 ]
