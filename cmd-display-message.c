/* $Id$ */

/*
 * Copyright (c) 2009 Tiago Cunha <me@tiagocunha.org>
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

#include <stdlib.h>
#include <time.h>

#include "tmux.h"

/*
 * Displays a message in the status line.
 */

enum cmd_retval	 cmd_display_message_exec(struct cmd *, struct cmd_q *);
void		 cmd_display_message_prepare(struct cmd *, struct cmd_q *);

const struct cmd_entry cmd_display_message_entry = {
	"display-message", "display",
	"c:pt:F:", 0, 1,
	"[-p] [-c target-client] [-F format] " CMD_TARGET_PANE_USAGE
	" [message]",
	0,
	0,
	NULL,
	cmd_display_message_exec,
	cmd_display_message_prepare
};

void
cmd_display_message_prepare(struct cmd *self, struct cmd_q *cmdq)
{
	struct args	*args = self->args;

	if (args_has(args, 't')) {
		cmdq->cmd_ctx.wl = cmd_find_pane(cmdq, args_get(args, 't'),
				&cmdq->cmd_ctx.s, &cmdq->cmd_ctx.wp);
	} else {
		cmdq->cmd_ctx.wl = cmd_find_pane(cmdq, NULL, &cmdq->cmd_ctx.s,
				&cmdq->cmd_ctx.wp);
	}

	if (args_has(args, 'c')) {
		cmdq->cmd_ctx.c = cmd_find_client(cmdq,
				args_get(args, 'c'), 0);
	} else {
		cmdq->cmd_ctx.c = cmd_current_client(cmdq);
	}
}

enum cmd_retval
cmd_display_message_exec(struct cmd *self, struct cmd_q *cmdq)
{
	struct args		*args = self->args;
	struct client		*c;
	struct session		*s;
	struct winlink		*wl;
	struct window_pane	*wp;
	const char		*template;
	char			*msg;
	struct format_tree	*ft;
	char			 out[BUFSIZ];
	time_t			 t;
	size_t			 len;

	if ((wl = cmdq->cmd_ctx.wl) == NULL)
		return (CMD_RETURN_ERROR);

	wp = cmdq->cmd_ctx.wp;
	s = cmdq->cmd_ctx.s;

	if (args_has(args, 'F') && args->argc != 0) {
		cmdq_error(cmdq, "only one of -F or argument must be given");
		return (CMD_RETURN_ERROR);
	}

	c = cmdq->cmd_ctx.c;
	if (args_has(args, 'c')) {
	    if (c == NULL)
		return (CMD_RETURN_ERROR);
	} else {
		if (c == NULL && !args_has(self->args, 'p')) {
			cmdq_error(cmdq, "no client available");
			return (CMD_RETURN_ERROR);
		}
	}

	template = args_get(args, 'F');
	if (args->argc != 0)
		template = args->argv[0];
	if (template == NULL)
		template = DISPLAY_MESSAGE_TEMPLATE;

	ft = format_create();
	if (c != NULL)
		format_client(ft, c);
	format_session(ft, s);
	format_winlink(ft, s, wl);
	format_window_pane(ft, wp);

	t = time(NULL);
	len = strftime(out, sizeof out, template, localtime(&t));
	out[len] = '\0';

	msg = format_expand(ft, out);
	if (args_has(self->args, 'p'))
		cmdq_print(cmdq, "%s", msg);
	else
		status_message_set(c, "%s", msg);
	free(msg);
	format_free(ft);

	return (CMD_RETURN_NORMAL);
}
