bin_PROGRAMS = rastertocapt

rastertocapt_SOURCES = \
	rastertocapt.c \
	std.h \
	word.h \
	capt-command.h capt-command.c \
	capt-status.h capt-status.c \
	generic-ops.h generic-ops.c \
	printer.h printer.c \
	paper.h paper.c \
	hiscoa-common.h hiscoa-common.c \
	hiscoa-compress.h hiscoa-compress.c \
	prn_lbp2900.c

rastertocapt_LDADD = $(CUPS_LIBS)

AM_CFLAGS = -std=c99 -Wall -Wextra -pedantic $(CUPS_CFLAGS)
