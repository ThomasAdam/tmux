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

#include <ctype.h>
#include <stdlib.h>

#include <string.h>

#include "tmux.h"

enum cmd_retval cmd_set_hook_exec(struct cmd *, struct cmd_q *);

const struct cmd_entry cmd_set_hook_entry = {
	"set-hook", NULL,
	"gn:t:u", 0, 1,
	"[-n hook-name] [-g]" CMD_TARGET_SESSION_USAGE " [-u hook-name] [command]",
	0,
	NULL,
	NULL,
	cmd_set_hook_exec
};

enum cmd_retval
cmd_set_hook_exec(struct cmd *self, struct cmd_q *cmdq)
{
	struct args	*args = self->args;
	struct session	*s;
	struct cmd_list	*cmdlist;
	struct hooks	*hooks_ent;
	struct hook	*hook;
	char		*cause;
	const char	*hook_name, *hook_cmd;

	if (args_has(args, 't'))
		if ((s = cmd_find_session(cmdq, args_get(args, 't'), 0)) == NULL)
			return (CMD_RETURN_ERROR);

	if (s == NULL && cmdq->client != NULL)
		s = cmdq->client->session;

	if ((hook_name = args_get(args, 'n')) == NULL) {
		cmdq_error(cmdq, "No hook name given.");
		return (CMD_RETURN_ERROR);
	}

	hooks_ent = args_has(args, 'g') ? &global_hooks : &s->hooks;

	if (s != NULL && args_has(args, 'u')) {
		hook = hooks_find(hooks_ent, (char *)hook_name);
		hook_remove(hooks_ent, hook);
		return (CMD_RETURN_NORMAL);
	}

	if (args->argc == 0) {
		cmdq_error(cmdq, "No command for hook '%s' given.", hook_name);
		return (CMD_RETURN_ERROR);
	}
	hook_cmd = args->argv[0];

	if (cmd_string_parse(hook_cmd, &cmdlist, NULL, 0, &cause) != 0) {
		if (cmdlist == NULL || cause != NULL) {
			log_debug("Hook error: (%s)", cause);
			cmdq_error(cmdq, "Hook error: (%s)", cause);
			return (CMD_RETURN_ERROR);
		}
	}

	if (cmdlist == NULL)
		return (CMD_RETURN_ERROR);

	hooks_add(hooks_ent, hook_name, cmdlist);
	return (CMD_RETURN_NORMAL);
}
