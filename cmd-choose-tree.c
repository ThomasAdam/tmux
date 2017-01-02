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

#define CMD_CHOOSE_TREE_WINDOW_ACTION "select-window -t '%%'"
#define CMD_CHOOSE_TREE_SESSION_ACTION "switch-client -t '%%'"

/*
 * Enter choice mode to choose a session and/or window.
 */

#define CHOOSE_TREE_SESSION_TEMPLATE				\
	"#{session_name}: #{session_windows} windows"		\
	"#{?session_grouped, (group ,}"				\
	"#{session_group}#{?session_grouped,),}"		\
	"#{?session_attached, (attached),}"
#define CHOOSE_TREE_WINDOW_TEMPLATE				\
	"#{window_index}: #{window_name}#{window_flags} "	\
	"\"#{pane_title}\""

static enum cmd_retval	cmd_choose_tree_exec(struct cmd *, struct cmdq_item *);

const struct cmd_entry cmd_choose_tree_entry = {
	.name = "choose-tree",
	.alias = NULL,

	.args = { "t:", 0, 1 },
	.usage = CMD_TARGET_PANE_USAGE " [template]",

	.tflag = CMD_PANE,

	.flags = 0,
	.exec = cmd_choose_tree_exec
};

const struct cmd_entry cmd_choose_session_entry = {
	.name = "choose-session",
	.alias = NULL,

	.args = { "t:", 0, 1 },
	.usage = CMD_TARGET_PANE_USAGE " [template]",

	.tflag = CMD_PANE,

	.flags = 0,
	.exec = cmd_choose_tree_exec
};

const struct cmd_entry cmd_choose_window_entry = {
	.name = "choose-window",
	.alias = NULL,

	.args = { "t:", 0, 1 },
	.usage = CMD_TARGET_PANE_USAGE " [template]",

	.tflag = CMD_PANE,

	.flags = 0,
	.exec = cmd_choose_tree_exec
};

static enum cmd_retval
cmd_choose_tree_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args		*args = self->args;
	struct window_pane	*wp = item->state.tflag.wp;

	window_pane_set_mode(wp, &window_choose2_mode, args);

	return (CMD_RETURN_NORMAL);
}
