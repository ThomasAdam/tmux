/* $OpenBSD$ */

/*
 * Copyright (c) 2012 Thomas Adam <thomas@xteddy.org>
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

#include <string.h>

#include "tmux.h"

static enum cmd_retval	cmd_choose_tree_exec(struct cmd *, struct cmdq_item *);

const struct cmd_entry cmd_choose_tree_entry = {
	.name = "choose-tree",
	.alias = NULL,

	.args = { "ut:", 0, 1 },
	.usage = "[-u] " CMD_TARGET_WINDOW_USAGE,

	.tflag = CMD_WINDOW,

	.flags = 0,
	.exec = cmd_choose_tree_exec
};

static enum cmd_retval
cmd_choose_tree_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args			*args = self->args;
	struct client			*c = item->state.c;
	struct winlink			*wl = item->state.tflag.wl, *wm;

	if (c == NULL) {
		cmdq_error(item, "no client available");
		return (CMD_RETURN_ERROR);
	}

	if (window_pane_set_mode(wl->window->active, &choose_tree_mode,
	    args) != 0) {
		return (CMD_RETURN_NORMAL);
	}

	return (CMD_RETURN_NORMAL);
}
