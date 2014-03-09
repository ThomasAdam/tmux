/* $Id$ */

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

#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/*
 * Set a global or session hook.
 */

enum cmd_retval cmd_set_hook_exec(struct cmd *, struct cmd_q *);
void		cmd_set_hook_prepare(struct cmd *, struct cmd_q *);

const struct cmd_entry cmd_set_hook_entry = {
	"set-hook", NULL,
	"gt:u", 1, 2,
	"[-gu]" CMD_TARGET_SESSION_USAGE " hook-name [command]",
	CMD_PREPARESESSION,
	NULL,
	cmd_set_hook_exec,
	NULL
};

enum cmd_retval
cmd_set_hook_exec(struct cmd *self, struct cmd_q *cmdq)
{
	struct args	*args = self->args;
	struct session	*s;
	struct cmd_list	*cmdlist;
	struct hooks	*hooks;
	struct hook	*hook;
	char		*cause;
	const char	*name, *cmd;

	if ((s = cmdq->current_state.s) == NULL)
		return (CMD_RETURN_ERROR);
	hooks = args_has(args, 'g') ? &global_hooks : &s->hooks;

	name = args->argv[0];
	if (*name == '\0') {
		cmdq_error(cmdq, "invalid hook name");
		return (CMD_RETURN_ERROR);
	}
	if (args->argc < 2)
		cmd = NULL;
	else
		cmd = args->argv[1];

	if (args_has(args, 'u')) {
		if (cmd != NULL) {
			cmdq_error(cmdq, "command passed to unset hook: %s",
			    name);
			return (CMD_RETURN_ERROR);
		}
		if ((hook = hooks_find(hooks, name)) != NULL)
			hooks_remove(hooks, hook);
		return (CMD_RETURN_NORMAL);
	}

	if (cmd == NULL) {
		cmdq_error(cmdq, "no command to set hook: %s", name);
		return (CMD_RETURN_ERROR);
	}
	if (cmd_string_parse(cmd, &cmdlist, NULL, 0, &cause) != 0) {
		if (cause != NULL) {
			cmdq_error(cmdq, "%s", cause);
			free(cause);
		}
		return (CMD_RETURN_ERROR);
	}
	hooks_add(hooks, name, cmdlist);
	cmd_list_free(cmdlist);

	return (CMD_RETURN_NORMAL);
}
