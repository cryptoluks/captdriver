/*
 * Copyright (C) 2013 Alexey Galakhov <agalakhov@gmail.com>
 * Copyright (C) 2016 Alexei Gordeev <KP1533TM2@gmail.com>
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
#include "word.h"
#include "capt-command.h"
#include "capt-status.h"
#include "generic-ops.h"
#include "hiscoa-common.h"
#include "hiscoa-compress.h"
#include "paper.h"
#include "printer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint16_t job;

struct lbp2900_ops_s {
	struct printer_ops_s ops;
	const uint8_t *gpio_init;
	size_t gpio_init_size;
	const uint8_t *gpio_blink;
	size_t gpio_blink_size;
	const struct capt_status_s * (*get_status) (void);
	void (*wait_ready) (void);
	/* Optional model-specific setup command sent during job prologue */
	uint16_t setup_cmd;
	const uint8_t *setup_data;
	size_t setup_data_size;
	bool skip_gpio_before_job; /* LBP3000 quirk: skip GPIO+wait before send_job_start */
};

/* Magic buffers sent during init sequences */
static const uint8_t job_begin_data[] = {
	0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t upload_data[] = {
	0xEE, 0xDB, 0xEA, 0xAD, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* GPIO data per printer family */
static const uint8_t lbp2900_gpio_blink[] = {
	0x00, 0x00, 0x01, 0x02, 0x01, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x01, 0x00,
};

static const uint8_t lbp2900_gpio_init[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

static const uint8_t lbp3010_gpio_blink[] = {
	0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t lbp3010_gpio_init[] = {
	0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Model-specific setup command data */
static const uint8_t lbp3000_setup_data[] = { 0x00, 0x00 };
static const uint8_t lbp6000_setup_data[] = { 0x01, 0x00 };

static const struct lbp2900_ops_s *get_lops(const struct printer_ops_s *ops)
{
	return container_of(ops, struct lbp2900_ops_s, ops);
}

static const struct capt_status_s *get_status(const struct printer_ops_s *ops)
{
	return get_lops(ops)->get_status();
}

static void wait_ready(const struct printer_ops_s *ops)
{
	get_lops(ops)->wait_ready();
}

static void send_job_start(uint8_t fg, uint16_t page)
{
	time_t rawtime = time(NULL);
	const struct tm *tm = localtime(&rawtime);
	uint8_t buf[72]; /* 32 header + 40 padding (no host/user/doc names) */
	uint8_t head[32] = {
		0x00, 0x00, 0x00, 0x00, LO(page), HI(page), 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		fg, 0x01, LO(job), HI(job),
		0xC4, 0xFF, /* -60 */
		0x88, 0xFF, /* -120 */
		LO(tm->tm_year), HI(tm->tm_year),
		(uint8_t) tm->tm_mon, (uint8_t) tm->tm_mday,
		(uint8_t) tm->tm_hour, (uint8_t) tm->tm_min, (uint8_t) tm->tm_sec,
		0x01,
	};
	memcpy(buf, head, sizeof(head));
	memset(buf + 32, 0, 40);
	capt_sendrecv(CAPT_JOB_SETUP, buf, sizeof(buf), NULL, 0);
}

/*
 * Common job prologue for all LBP2900-family printers.
 * Model differences are encoded in the lbp2900_ops_s struct.
 */
static void common_job_prologue(struct printer_state_s *state)
{
	const struct lbp2900_ops_s *lops = get_lops(state->ops);
	uint8_t buf[8];
	size_t size;

	capt_sendrecv(CAPT_IDENT, NULL, 0, NULL, 0);
	usleep(200000);
	capt_init_status();
	get_status(state->ops);

	capt_sendrecv(CAPT_START_0, NULL, 0, NULL, 0);
	capt_sendrecv(CAPT_JOB_BEGIN, job_begin_data,
			ARRAY_SIZE(job_begin_data), buf, &size);
	job = WORD(buf[2], buf[3]);

	if (!lops->skip_gpio_before_job) {
		/* All models use lbp3010-style GPIO init for job start */
		capt_sendrecv(CAPT_GPIO, lbp3010_gpio_init,
				sizeof(lbp3010_gpio_init), NULL, 0);
		wait_ready(state->ops);
	}

	/* Model-specific setup command (LBP3000, LBP6000) */
	if (lops->setup_cmd) {
		if (lops->setup_cmd == CAPT_LBP6000_SETUP_0) {
			/* LBP6000: setup before send_job_start */
			capt_sendrecv(lops->setup_cmd, lops->setup_data,
					lops->setup_data_size, NULL, 0);
			wait_ready(state->ops);
			send_job_start(1, 0);
		} else {
			/* LBP3000: send_job_start before setup */
			send_job_start(1, 0);
			capt_sendrecv(lops->setup_cmd, lops->setup_data,
					lops->setup_data_size, NULL, 0);
		}
	} else {
		send_job_start(1, 0);
	}

	wait_ready(state->ops);
}

static uint8_t get_fuser_mode(unsigned media_type)
{
	switch (media_type) {
	case 0x00: case 0x01: case 0x02: return 0x01; /* plain, thick, plain L */
	case 0x03: return 0x02; /* thick H */
	case 0x04: return 0x13; /* transparency */
	case 0x05: return 0x14; /* label */
	case 0x06: return 0x1C; /* envelope */
	default:   return 0x01;
	}
}

static bool lbp2900_page_prologue(struct printer_state_s *state,
		const struct page_dims_s *dims)
{
	const struct capt_status_s *status;
	uint8_t hiscoa_buf[16];
	size_t hiscoa_size;
	uint8_t fm = get_fuser_mode(dims->media_type);

	uint8_t pageparms[] = {
		0x00, 0x00, 0x30, 0x2A,
		dims->paper_size_code, 0x00, 0x00, 0x00,
		(uint8_t)(dims->ink_k << 2), 0x1C, 0x1C, 0x1C,
		dims->media_type, dims->media_adapt, 0x04, 0x00,
		0x01, 0x01, 0x02, (uint8_t)dims->toner_save, 0x00, 0x00,
		LO(dims->margin_height), HI(dims->margin_height),
		LO(dims->margin_width), HI(dims->margin_width),
		LO(dims->line_size), HI(dims->line_size),
		LO(dims->num_lines), HI(dims->num_lines),
		LO(dims->paper_width), HI(dims->paper_width),
		LO(dims->paper_height), HI(dims->paper_height),
		0x00, 0x00, fm, 0x00, 0x00, 0x00,
	};

	status = get_status(state->ops);
	if (FLAG(status, CAPT_FL_UNINIT1) || FLAG(status, CAPT_FL_UNINIT2)) {
		capt_sendrecv(CAPT_START_1, NULL, 0, NULL, 0);
		capt_sendrecv(CAPT_START_2, NULL, 0, NULL, 0);
		capt_sendrecv(CAPT_START_3, NULL, 0, NULL, 0);
		wait_ready(state->ops);

		capt_sendrecv(CAPT_UPLOAD_2, upload_data,
				ARRAY_SIZE(upload_data), NULL, 0);
		wait_ready(state->ops);
	}

	/* Wait for buffer space */
	{
		unsigned delay = CAPT_POLL_MIN_US;
		while (FLAG(get_status(state->ops), CAPT_FL_BUFFERFULL)) {
			usleep(delay);
			if (delay < CAPT_POLL_MAX_US)
				delay = delay * 2 < CAPT_POLL_MAX_US ? delay * 2 : CAPT_POLL_MAX_US;
		}
	}

	capt_multi_begin(CAPT_SET_PARMS);
	capt_multi_add(CAPT_SET_PARM_PAGE, pageparms, sizeof(pageparms));
	hiscoa_size = hiscoa_format_params(hiscoa_buf, sizeof(hiscoa_buf),
			&hiscoa_default_params);
	capt_multi_add(CAPT_SET_PARM_HISCOA, hiscoa_buf, hiscoa_size);
	capt_multi_add(CAPT_SET_PARM_1, NULL, 0);
	capt_multi_add(CAPT_SET_PARM_2, NULL, 0);
	capt_multi_send();

	return true;
}

static bool lbp2900_page_epilogue(struct printer_state_s *state,
		const struct page_dims_s *dims)
{
	const struct capt_status_s *status;
	unsigned delay;
	(void) dims;

	capt_send(CAPT_PRINT_DATA_END, NULL, 0);

	/* Wait until the page is received by the printer */
	delay = CAPT_POLL_MIN_US;
	while (1) {
		status = get_status(state->ops);
		if (status->page_received == status->page_decoding)
			break;
		usleep(delay);
		if (delay < CAPT_POLL_MAX_US)
			delay = delay * 2 < CAPT_POLL_MAX_US ? delay * 2 : CAPT_POLL_MAX_US;
	}

	send_job_start(2, status->page_decoding);
	wait_ready(state->ops);

	{
		uint8_t buf[2] = { LO(status->page_decoding), HI(status->page_decoding) };
		capt_sendrecv(CAPT_FIRE, buf, 2, NULL, 0);
	}
	wait_ready(state->ops);

	send_job_start(6, status->page_decoding);

	/*
	 * Don't wait for page to physically exit. Buffer-full check in
	 * page_prologue and page_completed check in job_epilogue handle
	 * flow control. This allows overlapping printing with the next
	 * page's compression/transfer for multi-page speedup.
	 */
	{
		const struct capt_status_s *st = get_status(state->ops);
		if (FLAG(st, CAPT_FL_NOPAPER2) || FLAG(st, CAPT_FL_NOPAPER1)) {
			if (!FLAG(st, CAPT_FL_PRINTING) && !FLAG(st, CAPT_FL_PROCESSING1)) {
				fprintf(stderr, "DEBUG: CAPT: no paper\n");
				return false;
			}
		}
	}

	return true;
}

static void lbp2900_job_epilogue(struct printer_state_s *state)
{
	uint8_t jbuf[2] = { LO(job), HI(job) };
	unsigned delay = CAPT_POLL_MIN_US;

	while (1) {
		const struct capt_status_s *status = get_status(state->ops);
		if (status->page_completed == status->page_decoding) {
			send_job_start(4, status->page_completed);
			break;
		}
		usleep(delay);
		if (delay < CAPT_POLL_MAX_US)
			delay = delay * 2 < CAPT_POLL_MAX_US ? delay * 2 : CAPT_POLL_MAX_US;
	}

	capt_sendrecv(CAPT_JOB_END, jbuf, 2, NULL, 0);
}

static void cancel_cleanup(struct printer_state_s *state)
{
	const struct lbp2900_ops_s *lops = get_lops(state->ops);
	const struct capt_status_s *status = get_status(state->ops);
	uint8_t jbuf[2] = { LO(job), HI(job) };

	capt_sendrecv(CAPT_GPIO, lops->gpio_init,
			lops->gpio_init_size, NULL, 0);
	send_job_start(4, status->page_completed);
	capt_sendrecv(CAPT_JOB_END, jbuf, 2, NULL, 0);
}

static void wait_for_button(struct printer_state_s *state, enum capt_flags flag)
{
	const struct lbp2900_ops_s *lops = get_lops(state->ops);

	capt_sendrecv(CAPT_GPIO, lops->gpio_blink,
			lops->gpio_blink_size, NULL, 0);
	wait_ready(state->ops);

	while (1) {
		const struct capt_status_s *status = get_status(state->ops);
		if (FLAG(status, flag)) {
			fprintf(stderr, "DEBUG: CAPT: button pressed\n");
			break;
		}
		usleep(200000);
	}

	capt_sendrecv(CAPT_GPIO, lops->gpio_init,
			lops->gpio_init_size, NULL, 0);
	wait_ready(state->ops);
}

static void lbp2900_wait_user(struct printer_state_s *state)
{
	wait_for_button(state, CAPT_FL_BUTTON);
}

static void lbp3010_wait_user(struct printer_state_s *state)
{
	wait_for_button(state, CAPT_FL_nERROR);
}

/* ---- Printer registrations ---- */

#define COMMON_OPS \
	.job_prologue = common_job_prologue, \
	.job_epilogue = lbp2900_job_epilogue, \
	.page_prologue = lbp2900_page_prologue, \
	.page_epilogue = lbp2900_page_epilogue, \
	.compress_band = ops_compress_band_hiscoa, \
	.send_band = ops_send_band_hiscoa, \
	.cancel_cleanup = cancel_cleanup

#define GPIO_2900 \
	.gpio_init = lbp2900_gpio_init, \
	.gpio_init_size = sizeof(lbp2900_gpio_init), \
	.gpio_blink = lbp2900_gpio_blink, \
	.gpio_blink_size = sizeof(lbp2900_gpio_blink)

#define GPIO_3010 \
	.gpio_init = lbp3010_gpio_init, \
	.gpio_init_size = sizeof(lbp3010_gpio_init), \
	.gpio_blink = lbp3010_gpio_blink, \
	.gpio_blink_size = sizeof(lbp3010_gpio_blink)

static struct lbp2900_ops_s lbp2900_ops = {
	.ops = { COMMON_OPS, .wait_user = lbp2900_wait_user },
	GPIO_2900,
	.get_status = capt_get_xstatus,
	.wait_ready = capt_wait_ready,
};
register_printer("LBP2900", lbp2900_ops.ops, WORKS);

static struct lbp2900_ops_s lbp3000_ops = {
	.ops = { COMMON_OPS, .wait_user = lbp2900_wait_user },
	GPIO_2900,
	.get_status = capt_get_xstatus,
	.wait_ready = capt_wait_ready,
	.skip_gpio_before_job = true,
	.setup_cmd = CAPT_LBP3000_SETUP_0,
	.setup_data = lbp3000_setup_data,
	.setup_data_size = sizeof(lbp3000_setup_data),
};
register_printer("LBP3000", lbp3000_ops.ops, EXPERIMENTAL);

static struct lbp2900_ops_s lbp3010_ops = {
	.ops = { COMMON_OPS, .wait_user = lbp3010_wait_user },
	GPIO_3010,
	.get_status = capt_get_xstatus_only,
	.wait_ready = capt_wait_xready_only,
};

static struct lbp2900_ops_s lbp6000_ops = {
	.ops = { COMMON_OPS, .wait_user = lbp3010_wait_user },
	GPIO_3010,
	.get_status = capt_get_xstatus_only,
	.wait_ready = capt_wait_xready_only,
	.setup_cmd = CAPT_LBP6000_SETUP_0,
	.setup_data = lbp6000_setup_data,
	.setup_data_size = sizeof(lbp6000_setup_data),
};

register_printer("LBP3010/LBP3018/LBP3050", lbp3010_ops.ops, WORKS);
register_printer("LBP3100/LBP3108/LBP3150", lbp3010_ops.ops, EXPERIMENTAL);
register_printer("LBP6000/LBP6018", lbp6000_ops.ops, EXPERIMENTAL);
