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

#include "paper.h"
#include <cups/raster.h>
#include <stdio.h>
#include <string.h>

/*
 * Determine the paper size code sent to the printer from page dimensions.
 * PageSize is in points (1/72 inch). We match with a tolerance of ±3 points
 * to handle rounding differences between PPD generators.
 */
static uint8_t get_paper_size_code(unsigned page_width, unsigned page_height)
{
	struct { unsigned w; unsigned h; uint8_t code; } sizes[] = {
		{ 595, 842, 0x02 },  /* A4 */
		{ 420, 595, 0x03 },  /* A5 */
		{ 499, 709, 0x07 },  /* B5 (JIS) */
		{ 522, 756, 0x0A },  /* Executive */
		{ 612, 1008, 0x0C }, /* Legal */
		{ 612, 792, 0x0D },  /* Letter */
		{ 459, 649, 0x15 },  /* EnvC5 */
		{ 297, 684, 0x16 },  /* Env10 */
		{ 279, 540, 0x17 },  /* EnvMonarch */
		{ 312, 624, 0x18 },  /* EnvDL */
		{ 216, 360, 0x40 },  /* 3x5 */
		{ 397, 567, 0xD4 },  /* PRC16K */
	};
	unsigned i;
	for (i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
		int dw = (int)page_width - (int)sizes[i].w;
		int dh = (int)page_height - (int)sizes[i].h;
		if (dw >= -3 && dw <= 3 && dh >= -3 && dh <= 3)
			return sizes[i].code;
	}
	return 0x02; /* default to A4 */
}

void page_set_dims(struct page_dims_s *dims, const struct cups_page_header2_s *header)
{
	dims->media_type = header->cupsMediaType;
	dims->paper_width  = header->cupsWidth;
	dims->paper_height = header->cupsHeight;
	dims->toner_save = header->cupsInteger[0];
	dims->ink_k = header->cupsInteger[1];
	dims->manual_duplex = header->cupsInteger[2];
	dims->num_lines = header->cupsHeight;
	dims->band_size = header->cupsRowCount;
	dims->margin_height = header->Margins[0];
	dims->margin_width = header->Margins[1];

	/*
	 * Determine LINESIZE for the Hi-SCoA compressor.
	 * The PPD encodes the correct LINESIZE in cupsRowFeed.
	 * Fall back to computing from page dimensions if not set.
	 */
	if (header->cupsRowFeed > 0) {
		dims->line_size = header->cupsRowFeed;
	} else {
		/* Compute from printable width at device resolution */
		unsigned pixels = header->cupsWidth;
		dims->line_size = (pixels + 7) / 8;
		/* Round up to 4-byte boundary for alignment */
		dims->line_size = (dims->line_size + 3) & ~3u;
	}

	/* Determine paper size code from page point dimensions */
	dims->paper_size_code = get_paper_size_code(
			header->PageSize[0], header->PageSize[1]);

	if (header->HWResolution[1] == 400)
		dims->media_adapt = 0x81;
	else
		dims->media_adapt = 0x11;

	fprintf(stderr, "DEBUG: CAPT: page dims: line_size=%u num_lines=%u "
			"band_size=%u paper=%ux%u size_code=0x%02x\n",
			dims->line_size, dims->num_lines, dims->band_size,
			dims->paper_width, dims->paper_height,
			dims->paper_size_code);
}
