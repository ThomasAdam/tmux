/* $OpenBSD$ */

/*
 * Copyright (c) 2009 Nicholas Marriott <nicm@users.sourceforge.net>
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
#include <string.h>

#include "tmux.h"

/*
 * Display panes on a client.
 *
 * Set or get pane default fg/bg colours.
 */

enum cmd_retval	 cmd_colour_pane_exec(struct cmd *, struct cmd_q *);
enum cmd_retval	 cmd_display_panes_exec(struct cmd *, struct cmd_q *);

const struct cmd_entry cmd_colour_pane_entry = {
	"colour-pane", "colourp",
	"gt:APW", 0, 1,
	CMD_TARGET_PANE_USAGE " [-A|P|W] colour-style",
	0,
	cmd_colour_pane_exec
};

const struct cmd_entry cmd_display_panes_entry = {
	"display-panes", "displayp",
	"t:", 0, 0,
	CMD_TARGET_CLIENT_USAGE,
	0,
	cmd_display_panes_exec
};

enum cmd_retval
cmd_display_panes_exec(struct cmd *self, struct cmd_q *cmdq)
{
	struct args	*args = self->args;
	struct client	*c;

	if ((c = cmd_find_client(cmdq, args_get(args, 't'), 0)) == NULL)
		return (CMD_RETURN_ERROR);

	server_set_identify(c);

	return (CMD_RETURN_NORMAL);
}

enum cmd_retval
cmd_colour_pane_exec(struct cmd *self, struct cmd_q *cmdq)
{
	struct args		*args = self->args;
	struct session		*s;
	struct winlink		*wl;
	struct window_pane	*wp;
	int			 ret, nflags = 0;
	struct grid_cell	 *gc;
	const char		 *str;



	if ((wl = cmd_find_pane(cmdq, args_get(args, 't'), &s, &wp)) == NULL)
		return (CMD_RETURN_ERROR);

	if (args_has(args, 'g')) nflags++;
	if (args_has(args, 'A')) nflags++;
	if (args_has(args, 'P')) nflags++;
	if (args_has(args, 'W')) nflags++;

	if (nflags == 0 || nflags > 1) {
		cmdq_error(cmdq, "need exactly 1 of -g, -A, -P, or -W");
		return (CMD_RETURN_ERROR);
	}

	if (args_has(args, 'g')) {
		if (args->argc > 0) {
			cmdq_error(cmdq, "don't specify style with -g");
			return (CMD_RETURN_ERROR);
		}

		gc = wp->window->apcolgc;
		if (gc == NULL)
			str = "\"\"";
		else
			str = style_tostring(gc);
		cmdq_print(cmdq, "active-pane %s", str);


		gc = wp->colgc;
		if (gc == NULL)
			str = "\"\"";
		else
			str = style_tostring(gc);
		cmdq_print(cmdq, "pane %s", str);


		gc = wp->window->colgc;
		if (gc == NULL)
			str = "\"\"";
		else
			str = style_tostring(gc);
		cmdq_print(cmdq, "window %s", str);

		return (CMD_RETURN_NORMAL);
	}

	if (args->argc == 0) {
		cmdq_error(cmdq, "need a style argument");
		return (CMD_RETURN_ERROR);
	}

	str = args->argv[0];
	if (*str == '\0')
		gc = NULL;
	else {
		gc = xmalloc(sizeof *gc);
		memcpy(gc, &grid_default_cell, sizeof *gc);
		ret = style_parse(&grid_default_cell, gc, str);

		if (ret == -1) {
			free(gc);
			cmdq_error(cmdq, "bad colour style");
			return (CMD_RETURN_ERROR);
		}
	}

	if (args_has(args, 'A')) {
		free(wp->window->apcolgc);
		wp->window->apcolgc = gc;
		server_redraw_window(wp->window);
		return (CMD_RETURN_NORMAL);
	}

	if (args_has(args, 'P')) {
		free(wp->colgc);
		wp->colgc = gc;
		wp->flags |= PANE_REDRAW;
		return (CMD_RETURN_NORMAL);
	}

	if (args_has(args, 'W')) {
		free(wp->window->colgc);
		wp->window->colgc = gc;
		server_redraw_window(wp->window);
		return (CMD_RETURN_NORMAL);
	}

	return (CMD_RETURN_NORMAL);
}
