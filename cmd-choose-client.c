/* $OpenBSD$ */

/*
 * Copyright (c) 2009 Nicholas Marriott <nicholas.marriott@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <ctype.h>
#include <stdlib.h>

#include "tmux.h"

/*
 * Enter client mode.
 */

static enum cmd_retval	cmd_choose_client_exec(struct cmd *,
			    struct cmdq_item *);

const struct cmd_entry cmd_choose_client_entry = {
	.name = "choose-client",
	.alias = NULL,

	.args = { "t:", 0, 1 },
	.usage = CMD_TARGET_PANE_USAGE " [template]",

	.tflag = CMD_PANE,

	.flags = 0,
	.exec = cmd_choose_client_exec
};

static enum cmd_retval
cmd_choose_client_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args		*args = self->args;
	struct window_pane	*wp = item->state.tflag.wp;

	if (server_client_how_many() != 0)
		window_pane_set_mode(wp, &window_client_mode, args);

	return (CMD_RETURN_NORMAL);
}
