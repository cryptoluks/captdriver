check_PROGRAMS = captdefilter test-hiscoa test-word

hiscoa_decompress = \
	hiscoa-decompress.c \
	hiscoa-decompress.h

captdefilter_SOURCES = captdefilter.c \
	$(hiscoa_decompress)

test_hiscoa_SOURCES = test-hiscoa.c \
	../src/hiscoa-compress.c \
	$(hiscoa_decompress)

test_word_SOURCES = test-word.c \
	../src/hiscoa-compress.c

AM_CFLAGS = -std=c99 -Wall -Wextra -pedantic -I../src

TESTS = run-tests.sh
EXTRA_DIST = run-tests.sh
