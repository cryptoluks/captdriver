/*
 * Copyright (C) 2013 Alexey Galakhov <agalakhov@gmail.com>
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "std.h"
#include "printer.h"
#include "paper.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cups/raster.h>


struct band_list_s {
	struct band_list_s *next;
	size_t size;
	uint8_t data[];
};

struct cached_page_s {
	struct page_dims_s dims;
	struct band_list_s *bands;
};

static const struct printer_ops_s *ops;
static struct printer_state_s *state;
static struct cached_page_s *cached_page;
static cups_raster_t *raster;

static uint8_t *linebuf;
static uint8_t *bandbuf;
static uint8_t *compbuf;

static size_t center_pixels(size_t small, size_t large, unsigned bpp)
{
	unsigned small_p;
	unsigned large_p;
	unsigned bypp;
	while (bpp % 8)
		bpp *= 2;
	bypp = bpp / 8;
	small_p = small / bypp;
	large_p = large / bypp;
	return bypp * ((large_p - small_p) / 2);
}

static void free_cached_page(struct cached_page_s *page)
{
	while (page->bands) {
		void *p = page->bands;
		page->bands = page->bands->next;
		free(p);
	}
	free(page);
}

static void free_state(void)
{
	if (state) {
		if (ops->free_state)
			ops->free_state(state);
		else
			free(state);
		state = NULL;
	}
}

static void free_buffers(void)
{
	if (compbuf) {
		free(compbuf);
		compbuf = NULL;
	}
	if (bandbuf) {
		free(bandbuf);
		bandbuf = NULL;
	}
	if (linebuf) {
		free(linebuf);
		linebuf = NULL;
	}
}

static void compress_page_data(struct printer_state_s *state,
		struct cached_page_s *page,
		cups_raster_t *raster,
		const struct cups_page_header2_s *header)
{
	const struct page_dims_s *dims = &page->dims;
	const unsigned compsize = 2 * dims->line_size * dims->band_size;

	struct band_list_s *last_band = NULL;
	unsigned i;
	unsigned iband;

	unsigned shiftb = 0;
	unsigned shiftl = 0;
	unsigned csize = header->cupsBytesPerLine;


	if (header->cupsBytesPerLine < dims->line_size) {
		csize = header->cupsBytesPerLine;
		shiftb = center_pixels(header->cupsBytesPerLine, dims->line_size,
				header->cupsBitsPerPixel);
	}

	if (dims->line_size < header->cupsBytesPerLine) {
		csize = dims->line_size;
		shiftl = center_pixels(dims->line_size, header->cupsBytesPerLine,
				header->cupsBitsPerPixel);
	}

	if (header->cupsBytesPerLine > dims->line_size)
		fprintf(stderr, "INFO: cutting line from %u to %u bytes\n",
				header->cupsBytesPerLine, dims->line_size);

	if (header->cupsHeight > dims->num_lines)
		fprintf(stderr, "INFO: cutting image from %u to %u lines\n",
				header->cupsHeight, dims->num_lines);

	linebuf = calloc(1, header->cupsBytesPerLine);
	if (! linebuf)
		abort();
	bandbuf = calloc(dims->line_size, dims->band_size);
	if (! bandbuf)
		abort();
	compbuf = calloc(1, compsize);
	if (! compbuf)
		abort();

	for (iband = 0; iband * dims->band_size < dims->num_lines; ++iband) {
		struct band_list_s *new_band;
		unsigned start = iband * dims->band_size;
		unsigned nlines = dims->band_size;
		unsigned iline;
		size_t size;
		memset(bandbuf, 0, dims->line_size * dims->band_size);
		if (nlines + start >= dims->num_lines)
			nlines = dims->num_lines - start;
		for (iline = 0; iline < nlines; ++iline) {
			if (iline + start < header->cupsHeight) {
				if (cupsRasterReadPixels(raster, linebuf, header->cupsBytesPerLine)
								!= header->cupsBytesPerLine) {
					fprintf(stderr, "ERROR: CAPT: broken raster file\n");
					exit(1);
				}
			} else {
				memset(linebuf, 0, header->cupsBytesPerLine);
			}
			memcpy(bandbuf + iline * dims->line_size + shiftb, linebuf + shiftl, csize);
		}
		{
		bool is_last = (start + nlines) >= dims->num_lines;
		size = state->ops->compress_band(state, compbuf, compsize,
				bandbuf, dims->line_size, nlines, is_last);
		}
		new_band = calloc(1, sizeof(struct band_list_s) + size);
		if (! new_band)
			abort();
		new_band->size = size;
		if (size)
			memcpy(new_band->data, compbuf, size);
		if (! page->bands)
			page->bands = new_band;
		if (last_band)
			last_band->next = new_band;
		last_band = new_band;
	}

	/* discard end of image, if any */
	for (i = dims->num_lines; i < header->cupsHeight; ++i)
		cupsRasterReadPixels(raster, linebuf, header->cupsBytesPerLine);

	free_buffers();
}

static void send_page_data(struct printer_state_s *state, const struct cached_page_s *page)
{
	const struct band_list_s *band;

	state->isend = 0;
	state->iband = 0;

	for (band = page->bands; band; band = band->next, ++state->iband)
		state->ops->send_band(state, band->data, band->size);
}

static void do_cancel(int s)
{
	(void) s;

	if (ops)
		ops->cancel_cleanup(state);

	if (raster) {
		cupsRasterClose(raster);
		raster = NULL;
	}

	free_buffers();

	if (cached_page) {
		free_cached_page(cached_page);
		cached_page = NULL;
	}

	if (state)
		free_state();

	exit(1);
}

static void do_print(int fd)
{
	bool in_job = false;
	ops = printer_detect();

	if (ops->alloc_state)
		state = ops->alloc_state();
	else
		state = calloc(1, sizeof(struct printer_state_s));
	state->ops = ops;
	state->ipage = 0;

	raster = cupsRasterOpen(fd, CUPS_RASTER_READ);

	fprintf(stderr, "DEBUG: CAPT: rendering\n");

	while (1) {
		if (! cached_page) {
			struct cups_page_header2_s header;

			if (! cupsRasterReadHeader2(raster, &header))
				break;

			cached_page = calloc(1, sizeof(struct cached_page_s));
			if (! cached_page)
				abort();

			state->ipage += 1;
			page_set_dims(&cached_page->dims, &header);
			compress_page_data(state, cached_page, raster, &header);
		}

		if (! in_job) {
			fprintf(stderr, "DEBUG: CAPT: start job\n");
			if (ops->job_prologue)
				ops->job_prologue(state);
			in_job = true;
		}

		fprintf(stderr, "DEBUG: CAPT: start page %u\n", state->ipage);
		if (ops->page_prologue) {
			if (cached_page->dims.manual_duplex && state->ipage > 1) {
				fprintf(stderr, "DEBUG: CAPT: manual duplex: press button\n");
				ops->wait_user(state);
			}
			if (! ops->page_prologue(state, &cached_page->dims)) {
				fprintf(stderr, "DEBUG: CAPT: can't start page\n");
				ops->wait_user(state);
				if (in_job) {
					if (ops->job_epilogue)
						ops->job_epilogue(state);
					in_job = false;
				}
				continue;
			}
		}

		send_page_data(state, cached_page);

		fprintf(stderr, "DEBUG: CAPT: end page %u\n", state->ipage);
		if (ops->page_epilogue) {
			if (! ops->page_epilogue(state, &cached_page->dims)) {
				fprintf(stderr, "DEBUG: CAPT: page not printed\n");
				ops->wait_user(state);
				continue;
			}
		}

		free_cached_page(cached_page);
		cached_page = NULL;
	}

	if (in_job) {
		fprintf(stderr, "DEBUG: CAPT: end job\n");
		if (ops->job_epilogue)
			ops->job_epilogue(state);
	}

	if (! state->ipage)
		fprintf(stderr, "ERROR: CAPT: no pages in job\n");

	cupsRasterClose(raster);
	free_state();
}


int main(int argc, char *argv[])
{
	int fd = 0;

#if _POSIX_C_SOURCE >= 199309L
	{
		struct sigaction act;

		memset(&act, 0, sizeof(act));
		act.sa_handler = SIG_IGN;
		sigaction(SIGPIPE, &act, NULL);

		act.sa_handler = do_cancel;
		sigemptyset(&act.sa_mask);
		sigaddset(&act.sa_mask, SIGINT);
		sigaction(SIGTERM, &act, NULL);
	}
#else
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, do_cancel);
#endif

	if (argc < 6 || argc > 7) {
		fprintf(stderr, "Usage: %s job-id user title copies options [file]\n", argv[0]);
		return 1;
	}

	if (argc == 7) {
		fd = open(argv[6], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "ERROR: unable to open %s: %s\n", argv[6], strerror(errno));
			return 1;
		}
	}

	fprintf(stderr, "DEBUG: CAPT: started\n");
	do_print(fd);
	fprintf(stderr, "DEBUG: CAPT: finished\n");

	if (argc == 7)
		close(fd);

	return 0;
}
